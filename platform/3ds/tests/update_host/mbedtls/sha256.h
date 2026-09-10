#pragma once
#include <CommonCrypto/CommonDigest.h>
typedef CC_SHA256_CTX mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context*p){}
static inline void mbedtls_sha256_free(mbedtls_sha256_context*p){}
static inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context*p,int n){return CC_SHA256_Init(p)?0:-1;}
static inline int mbedtls_sha256_update_ret(mbedtls_sha256_context*p,const unsigned char*b,size_t n){return CC_SHA256_Update(p,b,(CC_LONG)n)?0:-1;}
static inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context*p,unsigned char*b){return CC_SHA256_Final(b,p)?0:-1;}
