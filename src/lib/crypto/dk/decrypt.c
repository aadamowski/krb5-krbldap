#include "k5-int.h"
#include "dk.h"

#define K5CLENGTH 5 /* 32 bit net byte order integer + one byte seed */

krb5_error_code
krb5_dk_decrypt(enc, hash, key, usage, ivec, input, output)
     krb5_const struct krb5_enc_provider *enc;
     krb5_const struct krb5_hash_provider *hash;
     krb5_const krb5_keyblock *key;
     krb5_keyusage usage;
     krb5_const krb5_data *ivec;
     krb5_const krb5_data *input;
     krb5_data *output;
{
    krb5_error_code ret;
    size_t hashsize, blocksize, keybytes, keylength, enclen, plainlen;
    unsigned char *plaindata, *kedata, *kidata, *cksum;
    krb5_keyblock ke, ki;
    krb5_data d1, d2;
    unsigned char constantdata[K5CLENGTH];

    /* allocate and set up ciphertext and to-be-derived keys */

    (*(hash->hash_size))(&hashsize);
    (*(enc->block_size))(&blocksize);
    (*(enc->keysize))(&keybytes, &keylength);

    enclen = input->length - hashsize;

    if ((kedata = (unsigned char *) malloc(keylength)) == NULL)
	return(ENOMEM);
    if ((kidata = (unsigned char *) malloc(keylength)) == NULL) {
	free(kedata);
	return(ENOMEM);
    }
    if ((plaindata = (unsigned char *) malloc(enclen)) == NULL) {
	free(kidata);
	free(kedata);
	return(ENOMEM);
    }
    if ((cksum = (unsigned char *) malloc(hashsize)) == NULL) {
	free(plaindata);
	free(kidata);
	free(kedata);
	return(ENOMEM);
    }

    ke.contents = kedata;
    ke.length = keylength;
    ki.contents = kidata;
    ki.length = keylength;

    /* derive the keys */

    d1.data = constantdata;
    d1.length = K5CLENGTH;

    d1.data[0] = (usage>>24)&0xff;
    d1.data[1] = (usage>>16)&0xff;
    d1.data[2] = (usage>>8)&0xff;
    d1.data[3] = usage&0xff;

    d1.data[4] = 0xAA;

    if (ret = krb5_derive_key(enc, key, &ke, &d1))
	goto cleanup;

    d1.data[4] = 0x55;

    if (ret = krb5_derive_key(enc, key, &ki, &d1))
	goto cleanup;

    /* decrypt the ciphertext */

    d1.length = enclen;
    d1.data = input->data;

    d2.length = enclen;
    d2.data = plaindata;

    if (ret = ((*(enc->decrypt))(&ke, ivec, &d1, &d2)))
	goto cleanup;

    /* verify the hash */

    d1.length = hashsize;
    d1.data = cksum;

    if (ret = krb5_hmac(hash, &ki, 1, &d2, &d1))
	goto cleanup;

    if (memcmp(cksum, input->data+enclen, hashsize) != 0) {
	ret = KRB5KRB_AP_ERR_BAD_INTEGRITY;
	goto cleanup;
    }

    /* get the real plaintext length and copy the data into the output */

    plainlen = ((((plaindata+blocksize)[0])<<24) |
		(((plaindata+blocksize)[1])<<16) |
		(((plaindata+blocksize)[2])<<8) |
		((plaindata+blocksize)[3]));

    if (plainlen > (enclen - blocksize - 4))
	return(KRB5_BAD_MSIZE);

    if (output->length < plainlen)
	return(KRB5_BAD_MSIZE);

    output->length = plainlen;

    memcpy(output->data, d2.data+blocksize+4, output->length);

    ret = 0;

cleanup:
    memset(kedata, 0, keylength);
    memset(kidata, 0, keylength);
    memset(plaindata, 0, enclen);
    memset(cksum, 0, hashsize);

    free(cksum);
    free(plaindata);
    free(kidata);
    free(kedata);

    return(ret);
}
