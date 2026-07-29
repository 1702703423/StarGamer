#include "dl_string_lib.h"

#define STRING_EXTERN(func,index,ret,...)	DL_DECLARE_EXTERN_LIB_FUNC(func,DL_STRING_LIB_BASE,DL_STRING_LIB_NUM,index,ret,##__VA_ARGS__)

void *memchr(const void *str, int c, size_t n){
	STRING_EXTERN(memchr,MEMCHR,void*,const void *str, int c, size_t n);
	return DL_FUNC_NAME(memchr)(str,c,n);
}
int memcmp(const void *str1, const void *str2, size_t n){
	STRING_EXTERN(memcmp,MEMCMP,int,const void *str1, const void *str2, size_t n);
	return DL_FUNC_NAME(memcmp)(str1,str2,n);
}
void *memcpy(void *dest, const void *src, size_t n){
	STRING_EXTERN(memcpy,MEMCPY,void*,void *dest, const void *src, size_t n);
	return DL_FUNC_NAME(memcpy)(dest,src,n);
}
void *memmove(void *dest, const void *src, size_t n){
	STRING_EXTERN(memmove,MEMMOVE,void*,void *dest, const void *src, size_t n);
	return DL_FUNC_NAME(memmove)(dest,src,n);
}
void *memset(void *str, int c, size_t n){
	STRING_EXTERN(memset,MEMSET,void*,void *str, int c, size_t n);
	return DL_FUNC_NAME(memset)(str,c,n);
}
char *strcat(char *dest, const char *src){
	STRING_EXTERN(strcat,STRCAT,char*,char *dest, const char *src);
	return DL_FUNC_NAME(strcat)(dest,src);
}
char *strncat(char *dest, const char *src, size_t n){
	STRING_EXTERN(strncat,STRNCAT,char *,char *dest, const char *src, size_t n);
	return DL_FUNC_NAME(strncat)(dest,src,n);
}

char *strchr(const char *str, int c){
	STRING_EXTERN(strchr,STRCHR,char *,const char *str, int c);
	return DL_FUNC_NAME(strchr)(str,c);
}
int strcmp(const char *str1, const char *str2){
	STRING_EXTERN(strcmp,STRCMP,int,const char *str1, const char *str2);
	return DL_FUNC_NAME(strcmp)(str1,str2);
}
int strncmp(const char *str1, const char *str2, size_t n){
	STRING_EXTERN(strncmp,STRNCMP,int,const char *str1, const char *str2, size_t n);
	return DL_FUNC_NAME(strncmp)(str1,str2,n);
}
int strcoll(const char *str1, const char *str2){
	STRING_EXTERN(strcoll,STRCOLL,int,const char *str1, const char *str2);
	return DL_FUNC_NAME(strcoll)(str1,str2);
}
char *strcpy(char *dest, const char *src){
	STRING_EXTERN(strcpy,STRCPY,char*,char *dest, const char *src);
	return DL_FUNC_NAME(strcpy)(dest,src);
}
char *strncpy(char *dest, const char *src, size_t n){
	STRING_EXTERN(strncpy,STRNCPY,char *,char *dest, const char *src, size_t n);
	return DL_FUNC_NAME(strncpy)(dest,src,n);
}
size_t strcspn(const char *str1, const char *str2){
	STRING_EXTERN(strcspn,STRCSPN,size_t,const char *str1, const char *str2);
	return DL_FUNC_NAME(strcspn)(str1,str2);
}
char *strerror(int errnum){
	STRING_EXTERN(strerror,STRERROR,char *,int errnum);
	return DL_FUNC_NAME(strerror)(errnum);
}
size_t strlen(const char *str){
	STRING_EXTERN(strlen,STRLEN,size_t,const char *str);
	return DL_FUNC_NAME(strlen)(str);
}
char *strpbrk(const char *str1, const char *str2){
	STRING_EXTERN(strpbrk,STRPBRK,char *,const char *str1, const char *str2);
	return DL_FUNC_NAME(strpbrk)(str1,str2);
}
char *strrchr(const char *str, int c){
	STRING_EXTERN(strrchr,STRRCHR,char *,const char *str, int c);
	return DL_FUNC_NAME(strrchr)(str,c);
}
size_t strspn(const char *str1, const char *str2){
	STRING_EXTERN(strspn,STRSPN,size_t,const char *str1, const char *str2);
	return DL_FUNC_NAME(strspn)(str1,str2);
}
char *strstr(const char *haystack, const char *needle){
	STRING_EXTERN(strstr,STRSTR,char *,const char *haystack, const char *needle);
	return DL_FUNC_NAME(strstr)(haystack,needle);
}
char *strtok(char *str, const char *delim){
	STRING_EXTERN(strtok,STRTOK,char *,char *str, const char *delim);
	return DL_FUNC_NAME(strtok)(str,delim);
}
size_t strxfrm(char *dest, const char *src, size_t n){
	STRING_EXTERN(strxfrm,STRXFRM,size_t,char *dest, const char *src, size_t n);
	return DL_FUNC_NAME(strxfrm)(dest,src,n);
}
