

#define EVP_MD void

int HKDF(uint8_t *out_key, size_t out_len, const EVP_MD *digest,
	 const uint8_t *secret, size_t secret_len,
	 const uint8_t *salt, size_t salt_len,
	 const uint8_t *info, size_t info_len);


