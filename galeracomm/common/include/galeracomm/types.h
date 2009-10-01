#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <gu_byteswap.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Utility functions to read and write integers. This is done to avoid 
 * memory alignment problems that may arise on some platforms.
 *
 * Read functions:
 * @param from Pointer memory location to read from
 * @param fromlen Total length of from buffer
 * @param from_offset Position in buffer to read from
 * @param to Location to store read value
 * 
 * @return Offset to next byte after read value
 *
 * Write functions:
 * @param from Value to write
 * @param to Buffer to write
 * @param tolen Total len of to buffer
 * @param to_offset Location of to buffer where value should be written
 * 
 * @return Offset to location after written value.
 */

static inline size_t read_uint8(const void *from, const size_t fromlen,
				const size_t from_offset, 
				uint8_t *to)
{
     if (fromlen - from_offset < 1)
	  return 0;
     *to = *((uint8_t *) from + from_offset);
     return from_offset + 1;
}

static inline size_t read_uint16(const void *from, const size_t fromlen,
				 const size_t from_offset, 
				 uint16_t *to)
{
     if (fromlen - from_offset < 2)
	  return 0;
     memcpy(to, (uint8_t*) from + from_offset, 2);
     *to = gtoh16(*to);
     return from_offset + 2;
}

static inline size_t read_uint32(const void *from, const size_t fromlen,
				 const size_t from_offset, 
				 uint32_t *to)
{
     if (fromlen - from_offset < 4)
	  return 0;
     memcpy(to, (uint8_t *) from + from_offset , 4);
     *to = gtoh32(*to);
     return from_offset + 4;
}

static inline size_t read_uint64(const void *from, const size_t fromlen, 
				     const size_t from_offset, uint64_t *to)
{
     if (fromlen - from_offset < 8)
	  return 0;
     memcpy(to, (uint8_t *)from + from_offset, 8);
     *to = gtoh64(*to);
     return from_offset + 8;
}

static inline size_t write_uint8(const uint8_t from, void *to, 
				     const size_t tolen, const size_t to_offset)
{
     if (tolen - to_offset < 1)
	  return 0;
     *((uint8_t *) to + to_offset) = from;
     return to_offset + 1;
}

static inline size_t write_uint16(const uint16_t from, void *to,
				      const size_t tolen, const size_t to_offset)
{
     uint16_t w;
     if (tolen - to_offset < 2)
	  return 0;
     w = htog16(from);
     memcpy((uint8_t *) to + to_offset, &w, 2);
     return to_offset + 2;
}

static inline size_t write_uint32(const uint32_t from, void *to,
				      const size_t tolen, const size_t to_offset)
{
     uint32_t w;
     if (tolen - to_offset < 4)
	  return 0;
     w = htog32(from);
     memcpy((uint8_t *) to + to_offset, &w, 4);
     return to_offset + 4;
}

static inline size_t write_uint64(const uint64_t from, void *to,
				      const size_t tolen, const size_t to_offset)
{
     uint64_t w;
     if (tolen - to_offset < 8)
	  return 0;
     w = htog64(from);
     memcpy((uint8_t *)to + to_offset, &w, 8);
     return to_offset + 8;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TYPES_H */
