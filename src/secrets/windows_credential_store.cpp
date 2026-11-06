#include "secrets/windows_credential_store.hpp"

#include <windows.h>
#include <wincred.h>

#include <string>

namespace multistream {
namespace {

std::wstring to_wide(const std::string &text)
{
	if (text.empty()) {
		return {};
	}
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring result(static_cast<std::size_t>(size), L'\0');
	if (size > 0) {
		MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
	}
	return result;
}

SecretError make_error(const char *operation, DWORD error)
{
	SecretError::Kind kind = SecretError::Kind::Failure;
	switch (error) {
	case ERROR_NOT_FOUND:
	case ERROR_NO_SUCH_LOGON_SESSION:
	case ERROR_NOT_LOGGED_ON:
		kind = SecretError::Kind::Unavailable;
		break;
	case ERROR_ACCESS_DENIED:
	case ERROR_CANCELLED:
		kind = SecretError::Kind::AccessDenied;
		break;
	default:
		break;
	}
	return {kind, std::string("Credential Manager ") + operation + " failed (" + std::to_string(error) + ")"};
}

std::wstring target_name(const std::string &prefix, const std::string &destination_id)
{
	std::wstring target = to_wide(prefix);
	target += L'/';
	target += to_wide(destination_id);
	return target;
}

} // namespace

std::optional<SecretError> WindowsCredentialStore::put(const std::string &destination_id, const std::string &secret)
{
	const std::wstring target = target_name(target_, destination_id);

	CREDENTIALW credential = {};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = const_cast<LPWSTR>(target.c_str());
	credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
	credential.CredentialBlob = const_cast<LPBYTE>(reinterpret_cast<const BYTE *>(secret.data()));
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;

	if (!CredWriteW(&credential, 0)) {
		return make_error("CredWrite", GetLastError());
	}
	return std::nullopt;
}

SecretLookup WindowsCredentialStore::get(const std::string &destination_id)
{
	const std::wstring target = target_name(target_, destination_id);

	PCREDENTIALW credential = nullptr;
	if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
		const DWORD error = GetLastError();
		if (error == ERROR_NOT_FOUND) {
			return {std::nullopt, std::nullopt};
		}
		return {std::nullopt, make_error("CredRead", error)};
	}

	std::string secret(reinterpret_cast<const char *>(credential->CredentialBlob),
			   static_cast<std::size_t>(credential->CredentialBlobSize));
	CredFree(credential);
	return {std::move(secret), std::nullopt};
}

std::optional<SecretError> WindowsCredentialStore::remove(const std::string &destination_id)
{
	const std::wstring target = target_name(target_, destination_id);

	if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
		const DWORD error = GetLastError();
		if (error == ERROR_NOT_FOUND) {
			return std::nullopt;
		}
		return make_error("CredDelete", error);
	}
	return std::nullopt;
}

} // namespace multistream
