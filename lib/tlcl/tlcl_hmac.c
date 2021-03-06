/*
 * tlcl: HMAC functions
 * Copyright (C) 2012 V Lab Technologies
 * Author: Teddy Reed
 * Based on libtpm by J. Kravitz (IBM) (C) 2004
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#include <sha1.h>

#include <tlcl.h>
#include "tlcl_internal.h"

/* The user session and TPM authenticate each other using HMACs.
 * There is a required rolling set of odd, even nonces generated by the
 * user process and TPM which are updated for each new HMAC.
 *
 * The user session authenticates the TPM by verifying the returned HMAC
 * for a OSAP session which is keyed using the requested key handle.
 * The TPM authenticates the user session similarly when a command is used
 * with the returned OSAP handle, which must also include a keyed HMAC.
 * This process assures that both entities know can generate keyed HMACs
 * with the same AuthData.
 */

uint32_t TSS_CheckHMAC(const uint8_t *response, uint32_t command,
	const uint8_t *nonceOdd, const uint8_t *key, uint32_t keySize,
	const uint8_t *key2, uint32_t keySize2, ...)
{
	uint16_t tag; /* AUTH1 or AUTH2 */
	uint32_t size, offset;
	uint8_t ordinal[TPM_U32_SIZE];

	uint8_t paramDigest[TPM_HASH_SIZE];
	uint8_t authHmac[TPM_HASH_SIZE];
	SHA1_CTX ctx;

	uint32_t arg_size, arg_pos;
	va_list argp;

	FromTpmUint16(response, &tag);
	FromTpmUint32(response + TPM_U16_SIZE, &size);
	ToTpmUint32(ordinal, command);

	if (tag == TPM_TAG_RSP_COMMAND)
		return TPM_SUCCESS;

	if (response == NULL || nonceOdd == NULL || key == NULL)
		return TPM_E_NULL_ARG;

	/* This can only check an AUTH1 or AUTH2 */
	if (tag != TPM_TAG_RSP_AUTH1_COMMAND && tag != TPM_TAG_RSP_AUTH2_COMMAND)
		return TPM_E_HMAC_FAIL;

	/* If this is checking 2 auths, a second key is required */
	if (tag == TPM_TAG_RSP_AUTH2_COMMAND && key2 == NULL)
		return TPM_E_NULL_ARG;

	/* HASH input */
	sha1_starts(&ctx);
	sha1_update(&ctx, response + TPM_U16_SIZE + TPM_U32_SIZE, TPM_U32_SIZE);
	/* Check this: command should be TPM-endian? */
	sha1_update(&ctx, ordinal, TPM_U32_SIZE);
	va_start(argp, keySize2);
	for (;;) {
		arg_size = (uint32_t) va_arg(argp, uint32_t);
		if (arg_size == 0) break;
		arg_pos = (uint32_t) va_arg(argp, uint32_t);
		sha1_update(&ctx, response + arg_pos, arg_size);
	}
	va_end(argp);
	sha1_finish(&ctx, paramDigest);

	/* Command trails with:
	 *   AUTH1: EVEN_NONCE, authBool, HMAC
	 *   AUTH2: EVEN_NONCE1, authBool1, HMAC1, EVEN_NONCE2, authBool2, HMAC2
	 */

	offset = TPM_NONCE_SIZE + 1 + TPM_HASH_SIZE;
	if (tag == TPM_TAG_RSP_AUTH2_COMMAND)
		offset *= 2;

	/* AUTH1 */
	hmac_starts(&ctx, key, keySize);
	hmac_update(&ctx, paramDigest, TPM_HASH_SIZE);
	/* even nonce */
	hmac_update(&ctx, response + size - offset, TPM_NONCE_SIZE);
	hmac_update(&ctx, nonceOdd, TPM_NONCE_SIZE);
	/* continue auth bool */
	hmac_update(&ctx, response + size - offset + TPM_NONCE_SIZE, 1);
	hmac_finish(&ctx, key, keySize, authHmac);

	if (memcmp(authHmac, response + size - offset + TPM_NONCE_SIZE + 1, TPM_HASH_SIZE) != 0)
		return TPM_E_HMAC_FAIL;

	/* AUTH2 */
	if (tag == TPM_TAG_RSP_AUTH2_COMMAND) {
		offset /= 2;
		hmac_starts(&ctx, key2, keySize2);
		hmac_update(&ctx, paramDigest, TPM_HASH_SIZE);
		/* even nonce */
		hmac_update(&ctx, response + size - offset, TPM_NONCE_SIZE);
		hmac_update(&ctx, nonceOdd, TPM_NONCE_SIZE);
		/* continue auth bool */
		hmac_update(&ctx, response + size - offset + TPM_NONCE_SIZE, 1);
		hmac_finish(&ctx, key2, keySize2, authHmac);

		if (memcmp(authHmac, response + size - offset + TPM_NONCE_SIZE + 1, TPM_HASH_SIZE) != 0)
			return TPM_E_HMAC_FAIL;
	}

	return TPM_SUCCESS;
}

uint32_t TSS_AuthHMAC(uint8_t *digest,
	const uint8_t *key, uint32_t keySize, const uint8_t *nonce1, const uint8_t *nonce2,
	uint8_t authBool, ...)
{
	uint8_t paramDigest[TPM_HASH_SIZE];
	SHA1_CTX ctx;

	uint32_t arg_size;
	uint8_t *arg_data;
	va_list argp;

	if (nonce1 == NULL || nonce2 == NULL) {
		return TPM_E_NULL_ARG;
	}

	/* HASH input */
	sha1_starts(&ctx);
	va_start(argp, authBool);
	for (;;) {
		arg_size = (uint32_t) va_arg(argp, uint32_t);
		if (arg_size == 0) break;
		arg_data = (uint8_t *) va_arg(argp, uint32_t *);
		if (arg_data == NULL)
			return TPM_E_NULL_ARG;
		sha1_update(&ctx, arg_data, arg_size);
	}
	va_end(argp);
	sha1_finish(&ctx, paramDigest);

	/* Calculate HMAC */
	hmac_starts(&ctx, key, keySize);
	hmac_update(&ctx, paramDigest, TPM_HASH_SIZE);
	hmac_update(&ctx, nonce1, TPM_NONCE_SIZE);
	hmac_update(&ctx, nonce2, TPM_NONCE_SIZE);
	hmac_update(&ctx, &authBool, 1);
	hmac_finish(&ctx, key, keySize, digest);

	return TPM_SUCCESS;
}

