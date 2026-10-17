/// Score data file encryption and decryption algorithms

#if defined(PLATFORM_PS2)
static inline uint8_t scorecry_ror3(uint8_t v) {
	return uint8_t((v >> 3) | (v << 5));
}
// Encrypts backwards.
#define scoredat_encode_byte_at(p, feedback, key1, key2) { \
	p[0] = (p[0] - (key1 + feedback)); \
	feedback = p[0]; \
	feedback = (scorecry_ror3(feedback) ^ (key2)); \
}

// Decrypts forwards.
#define scoredat_decode_byte_at(p, tmp, key1, key2) { \
	tmp = p[1]; \
	tmp = (scorecry_ror3(tmp) ^ (key2)); \
	p[0] = (key1 + tmp + p[0]); \
}
#else

// Encrypts backwards.
#define scoredat_encode_byte_at(p, feedback, key1, key2) { \
	p[0] = (p[0] - (key1 + feedback)); \
	feedback = p[0]; \
	_AL = key2; \
	_asm { ror feedback, 3 } \
	feedback ^= _AL; \
}

// Decrypts forwards.
#define scoredat_decode_byte_at(p, tmp, key1, key2) { \
	tmp = p[1]; \
	_AL = key2; \
	_asm { ror tmp, 3 } \
	tmp ^= _AL; \
	p[0] = (key1 + tmp + p[0]); \
}
#endif
