/*
 * lib/des425/des.h
 *
 * Copyright 1991 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 * 
 *
 */

/*
 * Include file for the Data Encryption Standard library.
 */

/* only do the whole thing once	 */
#ifndef DES_DEFS
#define DES_DEFS

#include "k5-int.h"
#include "des_int.h"

typedef mit_des_cblock des_cblock;	/* crypto-block size */

/* Key schedule */
typedef mit_des_key_schedule des_key_schedule;

#define DES_KEY_SZ 	(sizeof(des_cblock))
#define DES_ENCRYPT	1
#define DES_DECRYPT	0

#ifndef NCOMPAT
#define C_Block des_cblock
#define Key_schedule des_key_schedule
#define ENCRYPT DES_ENCRYPT
#define DECRYPT DES_DECRYPT
#define KEY_SZ DES_KEY_SZ
#define string_to_key des_string_to_key
#define read_pw_string des_read_pw_string
#define random_key des_random_key
#define pcbc_encrypt des_pcbc_encrypt
#define key_sched des_key_sched
#define cbc_encrypt des_cbc_encrypt
#define cbc_cksum des_cbc_cksum
#define C_Block_print des_cblock_print
#define quad_cksum des_quad_cksum
typedef struct des_ks_struct bit_64;
#endif

#define des_cblock_print(x) des_cblock_print_file(x, stdout)

/*
 * Windows needs everything prototyped because we're exporting all the fuctions.
 */
void des_cbc_cksum();
KRB5_DLLIMP int KRB5_CALLCONV des_ecb_encrypt();
int des_cbc_encrypt();
void des_fixup_key_parity();
int des_check_key_parity();
KRB5_DLLIMP int KRB5_CALLCONV des_key_sched();
KRB5_DLLIMP int KRB5_CALLCONV des_new_random_key();
void des_init_random_number_generator();
KRB5_DLLIMP void KRB5_CALLCONV des_set_random_generator_seed();
void des_set_sequence_number();
void des_generate_random_block();
int des_pcbc_encrypt();
unsigned long des_quad_cksum();
int des_random_key();
krb5_error_code des_read_password();
int des_string_to_key();
int des_is_weak_key();

#endif	/* DES_DEFS */
