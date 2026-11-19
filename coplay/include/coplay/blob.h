// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_BLOB_H
#define COPLAY_BLOB_H

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "protocol.h"

namespace coplay {

using BlobData = std::shared_ptr<const std::vector<uint8_t>>;

/** Hash and size of some data, or an invalid reference if it is empty or too large. */
BlobRef MakeBlobRef(const std::vector<uint8_t> &data);

void WriteBlobRequest(ByteWriter &w, const BlobHash &hash, uint16_t index);
bool ReadBlobRequest(ByteReader &r, BlobHash &hash, uint16_t &index);

/** Payload of one BlobChunk packet. @return false if index is out of range */
bool BuildBlobChunk(const BlobHash &hash, const std::vector<uint8_t> &data, uint16_t index, std::vector<uint8_t> &payload);
bool ReadBlobChunkHeader(ByteReader &r, BlobChunkHeader &h);

/**
 * Small pieces of data (player pictures) that are passed around in chunks
 * and verified against their hash once complete. Not thread safe.
 */
class BlobStore
{
	public:
		struct Request
		{
			BlobHash hash{};
			uint16_t index = 0;
		};

		/** Adds data we have ourselves. */
		BlobRef Put(const std::vector<uint8_t> &data);
		BlobData Get(const BlobHash &hash) const;
		bool Has(const BlobRef &ref) const;

		/** Starts collecting the data for this reference, unless it is here already. */
		void Want(const BlobRef &ref);
		bool Wanted(const BlobHash &hash) const { return downloads.count(hash) != 0; }
		void Forget(const BlobHash &hash);

		/** @return true if this chunk completed the data */
		bool OnChunk(const BlobChunkHeader &header, const uint8_t *data, size_t size);

		/** Chunks to ask for now. A chunk is asked for again after retry_ms without an answer. */
		void Poll(uint64_t now_ms, std::vector<Request> &requests, size_t max_requests = 8, uint32_t retry_ms = 200);

	private:
		struct Download
		{
			BlobRef ref;
			std::vector<uint8_t> data;
			std::vector<bool> got;
			std::vector<uint64_t> asked_ms;
			uint32_t got_count = 0;
		};

		static constexpr size_t kMaxEntries = 64;

		std::map<BlobHash, BlobData> complete;
		std::map<BlobHash, Download> downloads;
};

} // namespace coplay

#endif // COPLAY_BLOB_H
