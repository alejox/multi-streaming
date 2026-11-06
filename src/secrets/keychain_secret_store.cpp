#include "secrets/keychain_secret_store.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace multistream {
namespace {

// Owns one Core Foundation reference.
template<typename T> class CfOwned {
public:
	explicit CfOwned(T value = nullptr) : value_(value) {}
	~CfOwned()
	{
		if (value_ != nullptr) {
			CFRelease(value_);
		}
	}
	CfOwned(CfOwned &&other) noexcept : value_(other.value_) { other.value_ = nullptr; }
	CfOwned(const CfOwned &) = delete;
	CfOwned &operator=(const CfOwned &) = delete;

	T get() const { return value_; }
	T *out() { return &value_; }

private:
	T value_;
};

CfOwned<CFStringRef> make_cf_string(const std::string &text)
{
	return CfOwned<CFStringRef>(CFStringCreateWithBytes(kCFAllocatorDefault,
							    reinterpret_cast<const UInt8 *>(text.data()),
							    static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8,
							    false));
}

CfOwned<CFDataRef> make_cf_data(const std::string &bytes)
{
	return CfOwned<CFDataRef>(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(bytes.data()),
					       static_cast<CFIndex>(bytes.size())));
}

std::string to_std_string(CFStringRef text)
{
	if (text == nullptr) {
		return {};
	}
	const CFIndex capacity =
		CFStringGetMaximumSizeForEncoding(CFStringGetLength(text), kCFStringEncodingUTF8) + 1;
	std::string buffer(static_cast<std::size_t>(capacity), '\0');
	if (!CFStringGetCString(text, buffer.data(), capacity, kCFStringEncodingUTF8)) {
		return {};
	}
	buffer.resize(std::char_traits<char>::length(buffer.c_str()));
	return buffer;
}

SecretError make_error(const char *operation, OSStatus status)
{
	SecretError::Kind kind = SecretError::Kind::Failure;
	switch (status) {
	case errSecInteractionNotAllowed:
	case errSecNotAvailable:
	case errSecNoSuchKeychain:
		kind = SecretError::Kind::Unavailable;
		break;
	case errSecAuthFailed:
	case errSecUserCanceled:
		kind = SecretError::Kind::AccessDenied;
		break;
	default:
		break;
	}

	const CfOwned<CFStringRef> description(SecCopyErrorMessageString(status, nullptr));
	return {kind, std::string("Keychain ") + operation + " failed (" + std::to_string(status) +
			      "): " + to_std_string(description.get())};
}

// Builds a query matching one generic password item.
CfOwned<CFMutableDictionaryRef> make_item_query(const std::string &service, const std::string &account)
{
	CfOwned<CFMutableDictionaryRef> query(CFDictionaryCreateMutable(
		kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
	const auto service_name = make_cf_string(service);
	const auto account_name = make_cf_string(account);
	CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(query.get(), kSecAttrService, service_name.get());
	CFDictionarySetValue(query.get(), kSecAttrAccount, account_name.get());
	return query;
}

} // namespace

std::optional<SecretError> KeychainSecretStore::put(const std::string &destination_id, const std::string &secret)
{
	const auto query = make_item_query(service_, destination_id);
	const auto value = make_cf_data(secret);

	CfOwned<CFMutableDictionaryRef> attributes(CFDictionaryCreateMutable(
		kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
	CFDictionarySetValue(attributes.get(), kSecValueData, value.get());

	OSStatus status = SecItemUpdate(query.get(), attributes.get());
	if (status == errSecItemNotFound) {
		CFDictionarySetValue(query.get(), kSecValueData, value.get());
		status = SecItemAdd(query.get(), nullptr);
	}
	if (status != errSecSuccess) {
		return make_error("write", status);
	}
	return std::nullopt;
}

SecretLookup KeychainSecretStore::get(const std::string &destination_id)
{
	const auto query = make_item_query(service_, destination_id);
	CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);

	CfOwned<CFTypeRef> result;
	const OSStatus status = SecItemCopyMatching(query.get(), result.out());
	if (status == errSecItemNotFound) {
		return {};
	}
	if (status != errSecSuccess) {
		return {std::nullopt, make_error("read", status)};
	}

	const auto data = static_cast<CFDataRef>(result.get());
	return {std::string(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
			    static_cast<std::size_t>(CFDataGetLength(data))),
		std::nullopt};
}

std::optional<SecretError> KeychainSecretStore::remove(const std::string &destination_id)
{
	const auto query = make_item_query(service_, destination_id);
	const OSStatus status = SecItemDelete(query.get());
	if (status != errSecSuccess && status != errSecItemNotFound) {
		return make_error("delete", status);
	}
	return std::nullopt;
}

} // namespace multistream
