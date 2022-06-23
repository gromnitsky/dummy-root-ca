#include <sys/stat.h>

#define OPENSSL_API_COMPAT 1*10000 + 1*100 + 1
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <glib.h>

// change this to a single call to `EVP_RSA_gen()` for openssl 3
EVP_PKEY* key_new(int numbits) {
  EVP_PKEY *key = EVP_PKEY_new();

  BIGNUM *e = BN_new();
  BN_set_word(e, RSA_F4);
  RSA *rsa = RSA_new();
  RSA_generate_key_ex(rsa, numbits, e, NULL);
  EVP_PKEY_assign_RSA(key, rsa);

  BN_free(e);
  return key;
}

int addext(X509 *subject, X509 *issuer, int nid, char *value) {
  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, issuer, subject, NULL, NULL, 0);
  X509_EXTENSION *ex = X509V3_EXT_conf_nid(NULL, &ctx, nid, value);
  if (!ex) return 0;
  X509_add_ext(subject, ex, -1);
  X509_EXTENSION_free(ex);
  return 1;
}

ASN1_INTEGER* rand_serial() {
  const int rand_bits = 159;    /* openssl v3.0.3 apps/include/apps.h */
  ASN1_INTEGER *ai = ASN1_INTEGER_new();
  BIGNUM *bn = BN_new();

  if (!BN_rand(bn, rand_bits, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY)) {
    BN_free(bn);
    return NULL;
  }

  BN_to_ASN1_INTEGER(bn, ai);
  BN_free(bn);
  return ai;
}

X509* cert(EVP_PKEY *key, EVP_PKEY *CA_key, X509* CA_crt,
           const char* CN, char* altname, int days,
           GError **err) {
  X509 *crt = X509_new();
  X509_set_version(crt, 2);

  // serial number
  ASN1_INTEGER *serial = rand_serial();
  if (!serial) {
    X509_free(crt);
    g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "pseudo-random number generator failed");
    return NULL;
  }
  X509_set_serialNumber(crt, serial);
  ASN1_INTEGER_free(serial);

  X509_gmtime_adj(X509_getm_notBefore(crt), 0);
  X509_gmtime_adj(X509_getm_notAfter(crt), 60*60*24*days);

  X509_set_pubkey(crt, key);

  // subject name
  X509_NAME *name = X509_get_subject_name(crt);
  if (!X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                  (unsigned char *)CN, -1, -1, 0)) {
    X509_free(crt);
    g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED, "invalid CN");
    return NULL;
  }
  X509_set_subject_name(crt, name);

  X509_set_issuer_name(crt, CA_crt ? X509_get_issuer_name(CA_crt) : name);

  X509 *issuer = CA_crt ? CA_crt : crt;
  addext(crt, issuer, NID_subject_key_identifier, "hash");
  addext(crt, issuer, NID_authority_key_identifier, "keyid:always");
  if (CA_key) {
    char buf[BUFSIZ];
    if (strlen(altname))
      snprintf(buf, sizeof buf, "%s", altname);
    else
      snprintf(buf, sizeof buf, "DNS:%s,IP:127.0.0.1", CN);
    if (!addext(crt, issuer, NID_subject_alt_name, buf)) {
      g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED, "invalid subjectAltName");
      X509_free(crt);
      return NULL;
    }
    addext(crt, issuer, NID_basic_constraints, "critical,CA:FALSE");
    addext(crt, issuer, NID_ext_key_usage, "serverAuth");
    addext(crt, issuer, NID_key_usage, "digitalSignature,nonRepudiation,keyEncipherment,keyAgreement");
  } else {
    addext(crt, issuer, NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    addext(crt, issuer, NID_key_usage, "critical,keyCertSign,cRLSign");
    addext(crt, issuer, NID_netscape_cert_type, "sslCA,emailCA,objCA");
  }

  if (!X509_sign(crt, CA_key ? CA_key : key, EVP_sha256())) {
    g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED, "signing failed");
    X509_free(crt);
    return NULL;
  }

  return crt;
}

int key_save(char *name, EVP_PKEY *key) {
  FILE *fp = fopen(name, "w");
  if (!fp) return 0;
  chmod(name, 0600);

  int r = 1;
  if (!PEM_write_PrivateKey(fp, key, NULL, NULL, 0, NULL, NULL)) r = 0;
  fclose(fp);
  return r;
}

EVP_PKEY* key_read(char *name) {
  FILE *fp = fopen(name, "r"); if (!fp) return NULL;
  return PEM_read_PrivateKey(fp, NULL, NULL, NULL);
}

int crt_save(char *name, X509 *crt) {
  FILE *fp = fopen(name, "w");
  if (!fp) return 0;

  int r = 1;
  char buf[BUFSIZ];
  X509_NAME_oneline(X509_get_subject_name(crt), buf, sizeof buf);
  fprintf(fp, "# %s\n", buf+4);
  if (!PEM_write_X509(fp, crt)) r = 0;
  fclose(fp);
  return r;
}

X509* cert_read(char *name) {
  FILE *fp = fopen(name, "r"); if (!fp) return NULL;
  return PEM_read_X509(fp, NULL, NULL, NULL);
}

EVP_PKEY* mk_key_load_or_create(char *file, int numbits, gboolean regenerate_all, GError **err) {
  EVP_PKEY *key = regenerate_all ? NULL : key_read(file);
  if (!key) {
    key = key_new(numbits);
    if (!key_save(file, key)) {
      g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                  "failed to make %s", file);
      EVP_PKEY_free(key);
      key = NULL;
    }
  }

  return key;
}

int mk_keys_and_certs(const char *out_dir, const char *CN, char *altname,
                      int numbits, int days, gboolean regenerate_all,
                      GError **err) {
  char file[PATH_MAX];

  snprintf(file, sizeof file, "%s/root.pem", out_dir);
  EVP_PKEY *root_key = mk_key_load_or_create(file, numbits, regenerate_all, err);
  if (!root_key) return 0;

  snprintf(file, PATH_MAX, "%s/%s.pem", out_dir, CN);
  EVP_PKEY *server_key = mk_key_load_or_create(file, numbits, regenerate_all, err);
  if (!server_key) {
    EVP_PKEY_free(root_key);
    return 0;
  }

  int r = 1;
  GError *err2 = NULL;
  X509 *root_crt = NULL, *server_crt = NULL;

  snprintf(file, sizeof file, "%s/root.crt", out_dir);
  root_crt = regenerate_all ? NULL : cert_read(file);
  if (!root_crt) {
    root_crt = cert(root_key, NULL, NULL,"Dummy Root CA", NULL, days, &err2);
    if (!root_crt) {
      r = 0;
      g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                  "failed to make root.crt: %s", err2->message);
      g_error_free(err2);
      goto mk_keys_and_certs_cleanup;
    }
    if (!crt_save(file, root_crt)) {
      r = 0;
      g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                  "failed to save root.crt");
      goto mk_keys_and_certs_cleanup;
    }
  }

  // always remake
  g_clear_error(&err2);
  snprintf(file, PATH_MAX, "%s/%s.crt", out_dir, CN);
  server_crt = cert(server_key, root_key, root_crt, CN, altname, days-1, &err2);
  if (!server_crt) {
    r = 0;
    g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "failed to make %s: %s", file, err2->message);
    g_error_free(err2);
    goto mk_keys_and_certs_cleanup;
  }
  if (!crt_save(file, server_crt)) {
    r = 0;
    g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                "failed to save %s", file);
  }

 mk_keys_and_certs_cleanup:
  X509_free(root_crt);
  X509_free(server_crt);
  if (root_key) EVP_PKEY_free(root_key);
  if (server_key) EVP_PKEY_free(server_key);
  return r;
}
