#include "k5-int.h"
#include "rsa-md5.h"
#include "des_int.h"	/* we cheat a bit and call it directly... */

/*
 * In Kerberos V5 Beta 5 and previous releases the RSA-MD5-DES implementation
 * did not follow RFC1510.  The folowing definitions control the compatibility
 * with these releases.
 *
 * If MD5_K5BETA_COMPAT is defined, then compatability mode is enabled.  That
 * means that both checksum functions are compiled and available for use and
 * the additional interface krb5_md5_crypto_compat_ctl() is defined.
 *
 * If MD5_K5BETA_COMPAT_DEF is defined and compatability mode is enabled, then
 * the compatible behaviour becomes the default.
 *
 */
#define MD5_K5BETA_COMPAT
#define MD5_K5BETA_COMPAT_DEF


/* Windows needs to these prototypes for the assignment below */

#ifdef MD5_K5BETA_COMPAT
krb5_error_code
krb5_md5_crypto_compat_sum_func PROTOTYPE((
	krb5_const krb5_pointer in,
	krb5_const size_t in_length,
	krb5_const krb5_pointer seed,
	krb5_const size_t seed_length,
	krb5_checksum FAR *outcksum));
#endif

krb5_error_code
krb5_md5_crypto_sum_func PROTOTYPE((
	krb5_const krb5_pointer in,
	krb5_const size_t in_length,
	krb5_const krb5_pointer seed,
	krb5_const size_t seed_length,
	krb5_checksum FAR *outcksum));

krb5_error_code
krb5_md5_crypto_verify_func PROTOTYPE((
	krb5_const krb5_checksum FAR *cksum,
	krb5_const krb5_pointer in,
	krb5_const size_t in_length,
	krb5_const krb5_pointer seed,
	krb5_const size_t seed_length));

static mit_des_cblock zero_ivec = { 0 };

static void
krb5_md5_calculate_cksum(md5ctx, confound, confound_length, in, in_length)
    krb5_MD5_CTX		*md5ctx;
    krb5_pointer	in;
    size_t		in_length;
    krb5_pointer	confound;
    size_t		confound_length;
{
    krb5_MD5Init(md5ctx);
    if (confound && confound_length)
	krb5_MD5Update(md5ctx, confound, confound_length);
    krb5_MD5Update(md5ctx, in, in_length);
    krb5_MD5Final(md5ctx);
}

#ifdef	MD5_K5BETA_COMPAT
krb5_error_code
krb5_md5_crypto_compat_sum_func(in, in_length, seed, seed_length, outcksum)
    krb5_const krb5_pointer in;
    krb5_const size_t in_length;
    krb5_const krb5_pointer seed;
    krb5_const size_t seed_length;
    krb5_checksum FAR *outcksum;
{
    krb5_octet outtmp[OLD_RSA_MD5_DES_CKSUM_LENGTH];
    krb5_octet *input = (krb5_octet *)in;
    krb5_encrypt_block eblock;
    krb5_keyblock keyblock;
    krb5_error_code retval;

    krb5_MD5_CTX working;

    krb5_MD5Init(&working);
    krb5_MD5Update(&working, input, in_length);
    krb5_MD5Final(&working);

    outcksum->checksum_type = CKSUMTYPE_RSA_MD5_DES;
    outcksum->length = OLD_RSA_MD5_DES_CKSUM_LENGTH;

    memcpy((char *)outtmp, (char *)&working.digest[0], 16);

    memset((char *)&working, 0, sizeof(working));

    keyblock.length = seed_length;
    keyblock.contents = (krb5_octet *)seed;
    keyblock.enctype = ENCTYPE_DES_CBC_MD5;

    if ((retval = mit_des_process_key(&eblock, &keyblock)))
	return retval;
    /* now encrypt it */
    retval = mit_des_cbc_encrypt((mit_des_cblock *)&outtmp[0],
				 (mit_des_cblock *)outcksum->contents,
				 OLD_RSA_MD5_DES_CKSUM_LENGTH,
				 (struct mit_des_ks_struct *)eblock.priv,
				 keyblock.contents,
				 MIT_DES_ENCRYPT);
    if (retval) {
	(void) mit_des_finish_key(&eblock);
	return retval;
    }
    return mit_des_finish_key(&eblock);
}
#endif	/* MD5_K5BETA_COMPAT */

krb5_error_code
krb5_md5_crypto_sum_func(in, in_length, seed, seed_length, outcksum)
    krb5_const krb5_pointer in;
    krb5_const size_t in_length;
    krb5_const krb5_pointer seed;
    krb5_const size_t seed_length;
    krb5_checksum FAR *outcksum;
{
    krb5_octet outtmp[NEW_RSA_MD5_DES_CKSUM_LENGTH];
    mit_des_cblock	tmpkey;
    krb5_encrypt_block eblock;
    krb5_keyblock keyblock;
    krb5_error_code retval;
    size_t i;
    krb5_MD5_CTX working;

    if (outcksum->length < NEW_RSA_MD5_DES_CKSUM_LENGTH)
	return KRB5_BAD_MSIZE;

    /* Generate the confounder in place */
    if ((retval = krb5_random_confounder(RSA_MD5_DES_CONFOUND_LENGTH, outtmp)))
	return(retval);

    /* Calculate the checksum */
    krb5_md5_calculate_cksum(&working,
			(krb5_pointer) outtmp,
			(size_t) RSA_MD5_DES_CONFOUND_LENGTH,
			in,
			in_length);

    outcksum->checksum_type = CKSUMTYPE_RSA_MD5_DES;
    outcksum->length = NEW_RSA_MD5_DES_CKSUM_LENGTH;

    /* Now blast in the digest */
    memcpy((char *)&outtmp[RSA_MD5_DES_CONFOUND_LENGTH],
	   (char *)&working.digest[0],
	   RSA_MD5_CKSUM_LENGTH);

    /* Clean up the droppings */
    memset((char *)&working, 0, sizeof(working));

    /* Set up the temporary copy of the key (see RFC 1510 section 6.4.5) */
    memset((char *) tmpkey, 0, sizeof(mit_des_cblock));
    for (i=0; (i<seed_length) && (i<sizeof(mit_des_cblock)); i++)
	tmpkey[i] = (((krb5_octet *) seed)[i]) ^ 0xf0;

    keyblock.length = sizeof(mit_des_cblock);
    keyblock.contents = (krb5_octet *) tmpkey;
    keyblock.enctype = ENCTYPE_DES_CBC_MD5;

    if ((retval = mit_des_process_key(&eblock, &keyblock)))
	return retval;
    /* now encrypt it */
    retval = mit_des_cbc_encrypt((mit_des_cblock *)&outtmp[0],
				 (mit_des_cblock *)outcksum->contents,
				 NEW_RSA_MD5_DES_CKSUM_LENGTH,
				 (struct mit_des_ks_struct *)eblock.priv,
				 zero_ivec,
				 MIT_DES_ENCRYPT);
    if (retval) {
	(void) mit_des_finish_key(&eblock);
	return retval;
    }
    return mit_des_finish_key(&eblock);
}

krb5_error_code
krb5_md5_crypto_verify_func(cksum, in, in_length, seed, seed_length)
    krb5_const krb5_checksum FAR *cksum;
    krb5_const krb5_pointer in;
    krb5_const size_t in_length;
    krb5_const krb5_pointer seed;
    krb5_const size_t seed_length;
{
    krb5_octet outtmp[NEW_RSA_MD5_DES_CKSUM_LENGTH];
    mit_des_cblock	tmpkey;
    krb5_encrypt_block eblock;
    krb5_keyblock keyblock;
    krb5_error_code retval;
    size_t i;

    krb5_MD5_CTX working;

    retval = 0;
    if (cksum->checksum_type == CKSUMTYPE_RSA_MD5_DES) {
#ifdef	MD5_K5BETA_COMPAT
	/*
	 * We have a backwards compatibility problem here.  Kerberos
	 * version 5 Beta 5 and previous releases did not correctly
	 * generate RSA-MD5-DES checksums.  The way that we can
	 * differentiate is by the length of the provided checksum.
	 * If it's only OLD_RSA_MD5_DES_CKSUM_LENGTH, then it's the
	 * old style, otherwise it's the correct implementation.
	 */
	if (cksum->length == OLD_RSA_MD5_DES_CKSUM_LENGTH) {
	    /*
	     * If we're verifying the Old Style (tm) checksum, then we can just
	     * recalculate the checksum and encrypt it and see if it's the
	     * same.
	     */

	    /* Recalculate the checksum with no confounder */
	    krb5_md5_calculate_cksum(&working,
				(krb5_pointer) NULL,
				(size_t) 0,
				in,
				in_length);

	    /* Use the key "as-is" */
	    keyblock.length = seed_length;
	    keyblock.contents = (krb5_octet *) seed;
	    keyblock.enctype = ENCTYPE_DES_CBC_MD5;

	    if ((retval = mit_des_process_key(&eblock, &keyblock)))
		return retval;
	    /* now encrypt the checksum */
	    retval = mit_des_cbc_encrypt((mit_des_cblock *)&working.digest[0],
					 (mit_des_cblock *)&outtmp[0],
					 OLD_RSA_MD5_DES_CKSUM_LENGTH,
					 (struct mit_des_ks_struct *)
					 	eblock.priv,
					 keyblock.contents,
					 MIT_DES_ENCRYPT);
	    if (retval) {
		(void) mit_des_finish_key(&eblock);
		return retval;
	    }
	    if ((retval = mit_des_finish_key(&eblock)))
		return(retval);

	    /* Compare the encrypted checksums */
	    if (memcmp((char *) &outtmp[0],
		       (char *) cksum->contents,
		       OLD_RSA_MD5_DES_CKSUM_LENGTH))
		retval = KRB5KRB_AP_ERR_BAD_INTEGRITY;
	}
	else
#endif	/* MD5_K5BETA_COMPAT */
	if (cksum->length == (NEW_RSA_MD5_DES_CKSUM_LENGTH)) {
	    /*
	     * If we're verifying the correct implementation, then we have
	     * to do a little more work because we must decrypt the checksum
	     * because it contains the confounder in it.  So, figure out
	     * what our key variant is and then do it!
	     */

	    /* Set up the variant of the key (see RFC 1510 section 6.4.5) */
	    memset((char *) tmpkey, 0, sizeof(mit_des_cblock));
	    for (i=0; (i<seed_length) && (i<sizeof(mit_des_cblock)); i++)
		tmpkey[i] = (((krb5_octet *) seed)[i]) ^ 0xf0;

	    keyblock.length = sizeof(mit_des_cblock);
	    keyblock.contents = (krb5_octet *) tmpkey;
	    keyblock.enctype = ENCTYPE_DES_CBC_MD5;

	    if ((retval = mit_des_process_key(&eblock, &keyblock)))
		return retval;
	    /* now decrypt it */
	    retval = mit_des_cbc_encrypt((mit_des_cblock *)cksum->contents,
					 (mit_des_cblock *)&outtmp[0],
					 NEW_RSA_MD5_DES_CKSUM_LENGTH,
					 (struct mit_des_ks_struct *)
					 	eblock.priv,
					 zero_ivec,
					 MIT_DES_DECRYPT);
	    if (retval) {
		(void) mit_des_finish_key(&eblock);
		return retval;
	    }
	    if ((retval = mit_des_finish_key(&eblock)))
		return(retval);

	    /* Now that we have the decrypted checksum, try to regenerate it */
	    krb5_md5_calculate_cksum(&working,
				(krb5_pointer) outtmp,
				(size_t) RSA_MD5_DES_CONFOUND_LENGTH,
				in,
				in_length);

	    /* Compare the checksums */
	    if (memcmp((char *) &outtmp[RSA_MD5_DES_CONFOUND_LENGTH],
		       (char *) &working.digest[0],
		       RSA_MD5_CKSUM_LENGTH))
		retval = KRB5KRB_AP_ERR_BAD_INTEGRITY;
	}
	else 
	    retval = KRB5KRB_AP_ERR_BAD_INTEGRITY;
    }
    else
	retval = KRB5KRB_AP_ERR_INAPP_CKSUM;

    /* Clean up droppings */
    memset((char *)&working, 0, sizeof(working));
    return(retval);
}

krb5_checksum_entry rsa_md5_des_cksumtable_entry =
#if	defined(MD5_K5BETA_COMPAT) && defined(MD5_K5BETA_COMPAT_DEF)
{
    0,
    krb5_md5_crypto_compat_sum_func,
    krb5_md5_crypto_verify_func,
    OLD_RSA_MD5_DES_CKSUM_LENGTH,
    1,					/* is collision proof */
    1,					/* uses key */
};
#else	/* MD5_K5BETA_COMPAT && MD5_K5BETA_COMPAT_DEF */
{
    0,
    krb5_md5_crypto_sum_func,
    krb5_md5_crypto_verify_func,
    NEW_RSA_MD5_DES_CKSUM_LENGTH,
    1,					/* is collision proof */
    1,					/* uses key */
};
#endif	/* MD5_K5BETA_COMPAT && MD5_K5BETA_COMPAT_DEF */

#ifdef	MD5_K5BETA_COMPAT
/*
 * Turn on/off compatible checksum generation.
 */
void
krb5_md5_crypto_compat_ctl(scompat)
    krb5_boolean	scompat;
{
    if (scompat) {
	rsa_md5_des_cksumtable_entry.sum_func = krb5_md5_crypto_compat_sum_func;
	rsa_md5_des_cksumtable_entry.checksum_length =
	    OLD_RSA_MD5_DES_CKSUM_LENGTH;
    }
    else {
	rsa_md5_des_cksumtable_entry.sum_func = krb5_md5_crypto_sum_func;
	rsa_md5_des_cksumtable_entry.checksum_length =
	    NEW_RSA_MD5_DES_CKSUM_LENGTH;
    }
}
#endif	/* MD5_K5BETA_COMPAT */

