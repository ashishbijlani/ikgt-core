/*
 * Copyright (c) 2015-2019 Intel Corporation.
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include "vmm_base.h"
#include "vmm_arch.h"
#include "vmm_asm.h"
#include "vmm_objects.h"
#include "dbg.h"
#include "lib/util.h"
#include "heap.h"
#include "device_sec_info.h"
#include "modules/security_info.h"

#ifdef DERIVE_KEY
#include "modules/crypto.h"
#endif

#ifdef DERIVE_KEY
static int get_max_svn_index(device_sec_info_v0_t *sec_info)
{
	uint32_t i, max_svn_idx = 0;

	if ((sec_info->num_seeds == 0) || (sec_info->num_seeds > BOOTLOADER_SEED_MAX_ENTRIES))
		return -1;

	for (i = 1; i < sec_info->num_seeds; i++) {
		if (sec_info->dseed_list[i].cse_svn > sec_info->dseed_list[i - 1].cse_svn) {
			max_svn_idx = i;
		}
	}

	return max_svn_idx;
}

void key_derive(device_sec_info_v0_t *sec_info)
{
	const char salt[] = "Attestation Keybox Encryption Key";
	const uint8_t *ikm;
	uint8_t *prk;
	uint32_t ikm_len;
	int max_svn_idx;

	max_svn_idx = get_max_svn_index(sec_info);
	if (max_svn_idx < 0) {
		print_info("VMM: failed to get max svn index\n");
		memset(sec_info, 0, sizeof(device_sec_info_v0_t));
		return;
	}

	ikm = sec_info->dseed_list[max_svn_idx].seed;
	ikm_len = 32;

	prk = sec_info->attkb_enc_key;

	if (hmac_sha256((const uint8_t *)salt, sizeof(salt), ikm, ikm_len, prk) != 1) {
		memset(sec_info, 0, sizeof(device_sec_info_v0_t));
		print_panic("VMM: failed to derive key!\n");
	}
}
#endif

static uint32_t _mov_info(void *dest, void *src)
{
	uint32_t dev_sec_info_size = *((uint32_t *)src);

	VMM_ASSERT_EX(!(dev_sec_info_size & 0x3ULL), "size of Tee boot info is not 32bit aligned!\n");

	memcpy(dest, src, dev_sec_info_size);
	memset(src, 0, dev_sec_info_size);
	barrier();

	return dev_sec_info_size;
}

uint32_t mov_sec_info(void *dest, void *src)
{
	static void *dev_sec_info;
	uint32_t dev_sec_info_size;

	D(VMM_ASSERT_EX(src || dest, "dest and src can't be NULL simultaneously!\n"));
	D(VMM_ASSERT_EX((dest || (dev_sec_info == NULL)), "When dest is NULL, dev_sec_info MUST be NULL!\n"));
	D(VMM_ASSERT_EX(src || dev_sec_info, "src and dev_sec_info can't be NULL simultaneously!\n"));

	if (dest == INTERNAL) {
		dev_sec_info_size = *((uint32_t *)src);
		dev_sec_info = mem_alloc(dev_sec_info_size);
		dest = dev_sec_info;
	}

	if (src == INTERNAL)
		src = dev_sec_info;

	dev_sec_info_size = _mov_info(dest, src);

	if (src == dev_sec_info) {
		mem_free(dev_sec_info);
		dev_sec_info = NULL;
	}

#ifdef DERIVE_KEY
	if (dest != dev_sec_info)
		key_derive((device_sec_info_v0_t *)dest);
#endif

	return dev_sec_info_size;
}