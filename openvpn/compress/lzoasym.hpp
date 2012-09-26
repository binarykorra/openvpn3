//
//  lzoasym.hpp
//  OpenVPN
//
//  Copyright (c) 2012 OpenVPN Technologies, Inc. All rights reserved.
//

#ifndef OPENVPN_COMPRESS_LZOASYM_H
#define OPENVPN_COMPRESS_LZOASYM_H

#include <openvpn/compress/lzoasym_impl.hpp>

// Implement asymmetrical LZO compression (only uncompress, don't compress)
// Should only be included by lzoselect.hpp

namespace openvpn {

  class CompressLZOAsym : public Compress
  {
  public:
    // magic number for LZO compression
    enum {
      LZO_COMPRESS = 0x66,
      LZO_COMPRESS_SWAP = 0x67,
    };

    OPENVPN_SIMPLE_EXCEPTION(lzo_init_failed);

    CompressLZOAsym(const Frame::Ptr& frame,
		    const SessionStats::Ptr& stats,
		    const bool support_swap_arg,
		    const bool asym_arg) // we are always asymmetrical, regardless of setting
      : Compress(frame, stats),
	support_swap(support_swap_arg)
    {
      OPENVPN_LOG_COMPRESS("LZO-ASYM init swap=" << support_swap_arg << " asym=" << asym_arg);
    }

    static void init_static()
    {
    }

    virtual const char *name() const { return "lzo-asym"; }

    void decompress_work(BufferAllocated& buf)
    {
      // initialize work buffer
      size_t zlen = frame->prepare(Frame::DECOMPRESS_WORK, work);

      // do uncompress
      const int err = lzo_asym_impl::lzo1x_decompress_safe(buf.c_data(), buf.size(), work.data(), &zlen);
      if (err != lzo_asym_impl::LZOASYM_E_OK)
	{
	  error(buf);
	  return;
	}
      OPENVPN_LOG_COMPRESS_VERBOSE("LZO-ASYM uncompress " << buf.size() << " -> " << zlen);
      work.set_size(zlen);
      buf.swap(work);
    }

    virtual void compress(BufferAllocated& buf, const bool hint)
    {
      // skip null packets
      if (!buf.size())
	return;

      // indicate that we didn't compress
      if (support_swap)
	do_swap(buf, NO_COMPRESS_SWAP);
      else
	buf.push_front(NO_COMPRESS);
    }

    virtual void decompress(BufferAllocated& buf)
    {
      // skip null packets
      if (!buf.size())
	return;

      const unsigned char c = buf.pop_front();
      switch (c)
	{
	case NO_COMPRESS_SWAP:
	  do_unswap(buf);
	case NO_COMPRESS:
	  break;
	case LZO_COMPRESS_SWAP:
	  do_unswap(buf);
	case LZO_COMPRESS:
	  decompress_work(buf);
	  break;
	default: 
	  error(buf); // unknown op
	}
    }

  private:
    const bool support_swap;
    BufferAllocated work;
  };

} // namespace openvpn

#endif // OPENVPN_COMPRESS_LZOASYM_H
