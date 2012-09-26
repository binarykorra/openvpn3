//
//  lzoasym_impl.hpp
//  OpenVPN
//
//  Copyright (c) 2012 OpenVPN Technologies, Inc. All rights reserved.
//

#ifndef OPENVPN_COMPRESS_LZOASYM_IMPL_H
#define OPENVPN_COMPRESS_LZOASYM_IMPL_H

#include <cstring> // for std::memcpy

#include <boost/cstdint.hpp> // for boost::uint32_t, etc.

// Implementation of asymmetrical LZO compression (only uncompress, don'z compress)

#define LZOASYM_CHECK_INPUT_OVERRUN(x) if (size_t(input_ptr_end - input_ptr) < size_t(x)) goto input_overrun
#define LZOASYM_CHECK_OUTPUT_OVERRUN(x) if (size_t(output_ptr_end - output_ptr) < size_t(x)) goto output_overrun
#define LZOASYM_CHECK_LOOKBEHIND_OVERRUN(match_ptr) if (match_ptr < output || match_ptr >= output_ptr) goto lookbehind_overrun
#define LZOASYM_ASSERT(cond) if (!(cond)) goto assert_fail

namespace openvpn {
  namespace lzo_asym_impl {
    enum {
      LZOASYM_E_OK=0,
      LZOASYM_E_EOF_NOT_FOUND=-1,
      LZOASYM_E_INPUT_NOT_CONSUMED=-2,
      LZOASYM_E_INPUT_OVERRUN=-3,
      LZOASYM_E_OUTPUT_OVERRUN=-4,
      LZOASYM_E_LOOKBEHIND_OVERRUN=-5,
      LZOASYM_E_ASSERT_FAILED=-6,
    };

    enum {
      LZOASYM_EOF_CODE=1,
      LZOASYM_M2_MAX_OFFSET=0x0800,
    };

    template <typename T>
    inline T get_mem(const void *p)
    {
      typedef volatile const T* cptr;
      return *cptr(p);
    }

    template <typename T>
    inline T set_mem(void *p, const T value)
    {
      typedef volatile T* ptr;
      *ptr(p) = value;
    }

    template <typename T>
    inline void copy_mem(void *dest, const void *src)
    {
      typedef volatile T* ptr;
      typedef volatile const T* cptr;
      *ptr(dest) = *cptr(src);
    }

    template <typename T>
    inline bool ptr_aligned_4(const T* a, const T* b)
    {
      return ((size_t(a) | size_t(b)) & 3) == 0;
    }

    template <typename T>
    inline size_t ptr_diff(const T* a, const T* b)
    {
      return a - b;
    }

    inline size_t get_u16(const unsigned char *p)
    {
      // NOTE: assumes little-endian and unaligned 16-bit access is okay.
      // For a slower alternative without these assumptions, try: p[0] | (p[1] << 8)
      return get_mem<boost::uint16_t>(p);
    }

    inline void copy_64(unsigned char *dest, const unsigned char *src)
    {
      // NOTE: assumes that 64-bit machines can do 64-bit unaligned access, and
      // 32-bit machines can do 32-bit unaligned access.
      if (sizeof(void *) == 8)
	{
	  copy_mem<boost::uint64_t>(dest, src);
	}
      else
	{
	  copy_mem<boost::uint32_t>(dest, src);
	  copy_mem<boost::uint32_t>(dest+4, src+4);
	}
    }

    // NOTE: we might write up to ten extra bytes after the end of the copy
    inline void incremental_copy_fast(unsigned char *dest, const unsigned char *src, int len)
    {
      while (dest - src < 8)
	{
	  copy_64(dest, src);
	  len -= dest - src;
	  dest += dest - src;
	}
      while (len > 0)
	{
	  copy_64(dest, src);
	  src += 8;
	  dest += 8;
	  len -= 8;
	}
    }

    inline void incremental_copy(unsigned char *dest, const unsigned char *src, int len)
    {
      do {
	*dest++ = *src++;
      } while (--len);
    }

    inline void memcpy_fast(unsigned char *dest, const unsigned char *src, int len)
    {
      while (len >= 8)
	{
	  copy_64(dest, src);
	  src += 8;
	  dest += 8;
	  len -= 8;
	}
      if (len >= 4)
	{
	  copy_mem<boost::uint32_t>(dest, src);
	  src += 4;
	  dest += 4;
	  len -= 4;
	}
      switch (len)
	{
	case 3:
	  *dest++ = *src++;
	case 2:
	  *dest++ = *src++;
	case 1:
	  *dest++ = *src++;
	}
    }

    int lzo1x_decompress_safe(const unsigned char *input,
			      size_t input_length,
			      unsigned char *output,
			      size_t *output_length)
    {
      unsigned char *output_ptr;
      const unsigned char *input_ptr;
      size_t z;
      const unsigned char *match_ptr;
      const unsigned char *const input_ptr_end = input + input_length;
      unsigned char *const output_ptr_end = output + *output_length;

      *output_length = 0;

      input_ptr = input;
      output_ptr = output;

      if (*input_ptr > 17)
	{
	  z = *input_ptr++ - 17;
	  if (z < 4)
	    goto match_next;
	  LZOASYM_ASSERT(z > 0);
	  LZOASYM_CHECK_OUTPUT_OVERRUN(z);
	  LZOASYM_CHECK_INPUT_OVERRUN(z+1);
	  do {
	    *output_ptr++ = *input_ptr++;
	  } while (--z > 0);
	  goto first_literal_run;
	}

      while ((input_ptr < input_ptr_end) && (output_ptr <= output_ptr_end))
	{
	  z = *input_ptr++;
	  if (z >= 16)
	    goto match_found;

	  // a literal run
	  if (z == 0)
	    {
	      LZOASYM_CHECK_INPUT_OVERRUN(1);
	      while (*input_ptr == 0)
		{
		  z += 255;
		  input_ptr++;
		  LZOASYM_CHECK_INPUT_OVERRUN(1);
		}
	      z += 15 + *input_ptr++;
	    }

	  // copy literals
	  {
	    LZOASYM_ASSERT(z > 0);
	    const size_t len = z + 3;
	    LZOASYM_CHECK_OUTPUT_OVERRUN(len);
	    LZOASYM_CHECK_INPUT_OVERRUN(len+1);
	    memcpy_fast(output_ptr, input_ptr, len);
	    input_ptr += len;
	    output_ptr += len;
	  }

	first_literal_run:
	  z = *input_ptr++;
	  if (z >= 16)
	    goto match_found;

	  match_ptr = output_ptr - (1 + LZOASYM_M2_MAX_OFFSET);
	  match_ptr -= z >> 2;
	  match_ptr -= *input_ptr++ << 2;

	  LZOASYM_CHECK_LOOKBEHIND_OVERRUN(match_ptr);
	  LZOASYM_CHECK_OUTPUT_OVERRUN(3);
	  *output_ptr++ = *match_ptr++;
	  *output_ptr++ = *match_ptr++;
	  *output_ptr++ = *match_ptr;
	  goto match_done;

	  // handle matches
	  do {
	  match_found:
	    if (z >= 64)            // LZO "M2" match
	      {
		match_ptr = output_ptr - 1;
		match_ptr -= (z >> 2) & 7;
		match_ptr -= *input_ptr++ << 3;
		z = (z >> 5) - 1;
		goto copy_match;
	      }
	    else if (z >= 32)       // LZO "M3" match
	      {
		z &= 31;
		if (z == 0)
		  {
		    LZOASYM_CHECK_INPUT_OVERRUN(1);
		    while (*input_ptr == 0)
		      {
			z += 255;
			input_ptr++;
			LZOASYM_CHECK_INPUT_OVERRUN(1);
		      }
		    z += 31 + *input_ptr++;
		  }

		match_ptr = output_ptr - 1;
		match_ptr -= get_u16(input_ptr) >> 2;
		input_ptr += 2;
	      }
	    else if (z >= 16)       // LZO "M4" match
	      {
		match_ptr = output_ptr;
		match_ptr -= (z & 8) << 11;
		z &= 7;
		if (z == 0)
		  {
		    LZOASYM_CHECK_INPUT_OVERRUN(1);
		    while (*input_ptr == 0)
		      {
			z += 255;
			input_ptr++;
			LZOASYM_CHECK_INPUT_OVERRUN(1);
		      }
		    z += 7 + *input_ptr++;
		  }

		match_ptr -= get_u16(input_ptr) >> 2;
		input_ptr += 2;
		if (match_ptr == output_ptr)
		  {
		    LZOASYM_ASSERT(z == 1);
		    *output_length = ptr_diff(output_ptr, output);
		    return (input_ptr == input_ptr_end ? LZOASYM_E_OK :
			    (input_ptr < input_ptr_end  ? LZOASYM_E_INPUT_NOT_CONSUMED : LZOASYM_E_INPUT_OVERRUN));
		  }
		match_ptr -= 0x4000;
	      }
	    else                    // LZO "M1" match
	      {
		match_ptr = output_ptr - 1;
		match_ptr -= z >> 2;
		match_ptr -= *input_ptr++ << 2;

		LZOASYM_CHECK_LOOKBEHIND_OVERRUN(match_ptr);
		LZOASYM_CHECK_OUTPUT_OVERRUN(2);
		*output_ptr++ = *match_ptr++;
		*output_ptr++ = *match_ptr;
		goto match_done;
	      }

	  copy_match:
	    {
	      LZOASYM_CHECK_LOOKBEHIND_OVERRUN(match_ptr);
	      LZOASYM_ASSERT(z > 0);
	      LZOASYM_CHECK_OUTPUT_OVERRUN(z+3-1);

	      const size_t len = z + 2;
	      if (size_t(output_ptr_end - output_ptr) >= len + 10) // incremental_copy_fast might copy 10 more bytes than needed
		incremental_copy_fast(output_ptr, match_ptr, len);
	      else
		incremental_copy(output_ptr, match_ptr, len);
	      match_ptr += len;
	      output_ptr += len;
	    }

	  match_done:
	    z = input_ptr[-2] & 3;
	    if (z == 0)
	      break;

	  match_next:
	    // copy literals
	    LZOASYM_ASSERT(z > 0);
	    LZOASYM_ASSERT(z < 4);
	    LZOASYM_CHECK_OUTPUT_OVERRUN(z);
	    LZOASYM_CHECK_INPUT_OVERRUN(z+1);
	    *output_ptr++ = *input_ptr++;
	    if (z > 1)
	      {
		*output_ptr++ = *input_ptr++;
		if (z > 2)
		  *output_ptr++ = *input_ptr++;
	      }
	    z = *input_ptr++;
	  } while ((input_ptr < input_ptr_end) && (output_ptr <= output_ptr_end));
	}

      // no EOF code was found
      *output_length = ptr_diff(output_ptr, output);
      return LZOASYM_E_EOF_NOT_FOUND;

    input_overrun:
      *output_length = ptr_diff(output_ptr, output);
      return LZOASYM_E_INPUT_OVERRUN;

    output_overrun:
      *output_length = ptr_diff(output_ptr, output);
      return LZOASYM_E_OUTPUT_OVERRUN;

    lookbehind_overrun:
      *output_length = ptr_diff(output_ptr, output);
      return LZOASYM_E_LOOKBEHIND_OVERRUN;

    assert_fail:
      return LZOASYM_E_ASSERT_FAILED;
    }
  }
}

#undef LZOASYM_CHECK_INPUT_OVERRUN
#undef LZOASYM_CHECK_OUTPUT_OVERRUN
#undef LZOASYM_CHECK_LOOKBEHIND_OVERRUN
#undef LZOASYM_ASSERT

#endif
