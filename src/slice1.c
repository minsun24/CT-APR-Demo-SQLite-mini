#include <assert.h>
#include <string.h>
#include <stdio.h>

#define JSON_INVALID_CHAR 0x99999
typedef unsigned char u8;
typedef unsigned int u32;
static u32 jsonBytesToBypass(const char *z, u32 n);
static int jsonLabelCompare(
  const char *zLeft,          /* The left label */
  u32 nLeft,                  /* Size of the left label in bytes */
  int rawLeft,                /* True if zLeft contains no escapes */
  const char *zRight,         /* The right label */
  u32 nRight,                 /* Size of the right label in bytes */
  int rawRight                /* True if zRight is escape-free */
);
static __declspec(noinline) int jsonLabelCompareEscaped(
  const char *zLeft,          /* The left label */
  u32 nLeft,                  /* Size of the left label in bytes */
  int rawLeft,                /* True if zLeft contains no escapes */
  const char *zRight,         /* The right label */
  u32 nRight,                 /* Size of the right label in bytes */
  int rawRight                /* True if zRight is escape-free */
);
static u32 jsonUnescapeOneChar(const char*, u32, u32*);
static int sqlite3Utf8ReadLimited(const u8*, int, u32*);
static u8 jsonHexToInt(int h);
static u32 jsonHexToInt4(const char *z);


static const unsigned char sqlite3Utf8Trans1[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00,
};

static u32 jsonHexToInt4(const char *z){
  u32 v;
  v = (jsonHexToInt(z[0])<<12)
    + (jsonHexToInt(z[1])<<8)
    + (jsonHexToInt(z[2])<<4)
    + jsonHexToInt(z[3]);
  return v;
}
static u8 jsonHexToInt(int h){
  h += 9*(1&(h>>6));
  return (u8)(h & 0xf);
}
static u32 jsonBytesToBypass(const char *z, u32 n){
  u32 i = 0;
  while( i+1<n ){
    if( z[i]!='\\' ) return i;
    if( z[i+1]=='\n' ){
      i += 2;
      continue;
    }
    if( z[i+1]=='\r' ){
      if( i+2<n && z[i+2]=='\n' ){
        i += 3;
      }else{
        i += 2;
      }
      continue;
    }
    if( 0xe2==(u8)z[i+1]
     && i+3<n
     && 0x80==(u8)z[i+2]
     && (0xa8==(u8)z[i+3] || 0xa9==(u8)z[i+3])
    ){
      i += 4;
      continue;
    }
    break;
  }
  return i;
}

static int jsonLabelCompare(
  const char *zLeft,          /* The left label */
  u32 nLeft,                  /* Size of the left label in bytes */
  int rawLeft,                /* True if zLeft contains no escapes */
  const char *zRight,         /* The right label */
  u32 nRight,                 /* Size of the right label in bytes */
  int rawRight                /* True if zRight is escape-free */
){
  if( rawLeft && rawRight ){
    /* Simpliest case:  Neither label contains escapes.  A simple
    ** memcmp() is sufficient. */
    if( nLeft!=nRight ) return 0;
    return memcmp(zLeft, zRight, nLeft)==0;
  }else{
    return jsonLabelCompareEscaped(zLeft, nLeft, rawLeft,
                                   zRight, nRight, rawRight);
  }
}


static __declspec(noinline) int jsonLabelCompareEscaped(
  const char *zLeft,          /* The left label */
  u32 nLeft,                  /* Size of the left label in bytes */
  int rawLeft,                /* True if zLeft contains no escapes */
  const char *zRight,         /* The right label */
  u32 nRight,                 /* Size of the right label in bytes */
  int rawRight                /* True if zRight is escape-free */
){
  u32 cLeft, cRight;
  assert( rawLeft==0 || rawRight==0 );
  while( 1 /*exit-by-return*/ ){
    if( nLeft==0 ){
      cLeft = 0;
    }else if( rawLeft || zLeft[0]!='\\' ){
      cLeft = ((u8*)zLeft)[0];
      if( cLeft>=0xc0 ){
        int sz = sqlite3Utf8ReadLimited((u8*)zLeft, nLeft, &cLeft);
        zLeft += sz;
        nLeft -= sz;
      }else{
        zLeft++;
        nLeft--;
      }
    }else{
      u32 n = jsonUnescapeOneChar(zLeft, nLeft, &cLeft);
      zLeft += n;
      assert( n<=nLeft );
      nLeft -= n;
    }
    if( nRight==0 ){
      cRight = 0;
    }else if( rawRight || zRight[0]!='\\' ){
      cRight = ((u8*)zRight)[0];
      if( cRight>=0xc0 ){
        int sz = sqlite3Utf8ReadLimited((u8*)zRight, nRight, &cRight);
        zRight += sz;
        nRight -= sz;
      }else{
        zRight++;
        nRight--;
      }
    }else{
      u32 n = jsonUnescapeOneChar(zRight, nRight, &cRight);
      zRight += n;
      assert( n<=nRight );
      nRight -= n;
    }
    if( cLeft!=cRight ) return 0;
    if( cLeft==0 ) return 1;
  }
}

static u32 jsonUnescapeOneChar(const char *z, u32 n, u32 *piOut){
  assert( n>0 );
  assert( z[0]=='\\' );
  if( n<8 ){  /// buggy
    *piOut = JSON_INVALID_CHAR;
    return n;
  }
  switch( (u8)z[1] ){
    case 'u': {
      u32 v, vlo;
      if( n<6 ){
        *piOut = JSON_INVALID_CHAR;
        return n;
      }
      v = jsonHexToInt4(&z[2]);
      if( (v & 0xfc00)==0xd800
       && n>=12
       && z[6]=='\\'
       && z[7]=='u'
       && ((vlo = jsonHexToInt4(&z[8]))&0xfc00)==0xdc00
      ){
        *piOut = ((v&0x3ff)<<10) + (vlo&0x3ff) + 0x10000;
        return 12;
      }else{
        *piOut = v;
        return 6;
      }
    }
    case 'b': {   *piOut = '\b';  return 2; }
    case 'f': {   *piOut = '\f';  return 2; }
    case 'n': {   *piOut = '\n';  return 2; }
    case 'r': {   *piOut = '\r';  return 2; }
    case 't': {   *piOut = '\t';  return 2; }
    case 'v': {   *piOut = '\v';  return 2; }
    case '0': {   *piOut = 0;     return 2; }
    case '\'':
    case '"':
    case '/':
    case '\\':{   *piOut = z[1];  return 2; }
    case 'x': {
      if( n<4 ){
        *piOut = JSON_INVALID_CHAR;
        return n;
      }
      *piOut = (jsonHexToInt(z[2])<<4) | jsonHexToInt(z[3]);
      return 4;
    }
    case 0xe2:
    case '\r':
    case '\n': {
      u32 nSkip = jsonBytesToBypass(z, n);
      if( nSkip==0 ){
        *piOut = JSON_INVALID_CHAR;
        return n;
      }else if( nSkip==n ){
        *piOut = 0;
        return n;
      }else if( z[nSkip]=='\\' ){
        return nSkip + jsonUnescapeOneChar(&z[nSkip], n-nSkip, piOut);
      }else{
        int sz = sqlite3Utf8ReadLimited((u8*)&z[nSkip], n-nSkip, piOut);
        return nSkip + sz;
      }
    }
    default: {
      *piOut = JSON_INVALID_CHAR;
      return 2;
    }
  }
}

int sqlite3Utf8ReadLimited(
  const u8 *z,
  int n,
  u32 *piOut
){
  u32 c;
  int i = 1;
  assert( n>0 );
  c = z[0];
  if( c>=0xc0 ){
    c = sqlite3Utf8Trans1[c-0xc0];
    if( n>4 ) n = 4;
    while( i<n && (z[i] & 0xc0)==0x80 ){
      c = (c<<6) + (0x3f & z[i]);
      i++;
    }
  }
  *piOut = c;
  return i;
}
