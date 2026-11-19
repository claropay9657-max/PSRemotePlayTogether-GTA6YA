// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/blob.h>

#include <openssl/evp.h>

#include <algorithm>
#include <cstring>

namespace coplay {

BlobRef MakeBlobRef(const std::vector<uint8_t> &data)
{
	BlobRef ref;
	if(data.empty() || data.size() > kMaxAvatarSize)
		return ref;
	uint8_t digest[EVP_MAX_MD_SIZE];
	unsigned int digest_len = 0;
	if(EVP_Digest(data.data(), data.size(), digest, &digest_len, EVP_sha256(), nullptr) != 1 || digest_len < kBlobHashSize)
		return ref;
	std::memcpy(ref.hash.data(), digest, kBlobHashSize);
	ref.size = static_cast<uint16_t>(data.size());
	return ref;
}

void WriteBlobRequest(ByteWriter &w, const BlobHash &hash, uint16_t index)
{
	w.Bytes(hash.data(), hash.size());
	w.U16(index);
}

bool ReadBlobRequest(ByteReader &r, BlobHash &hash, uint16_t &index)
{
	r.Bytes(hash.data(), hash.size());
	index = r.U16();
	return r.ok();
}

bool BuildBlobChunk(const BlobHash &hash, const std::vector<uint8_t> &data, uint16_t index, std::vector<uint8_t> &payload)
{
	uint32_t count = BlobChunkCount(data.size());
	if(index >= count)
		return false;
	size_t offset = static_cast<size_t>(index) * kBlobChunkSize;
	size_t len = std::min(kBlobChunkSize, data.size() - offset);
	payload.clear();
	ByteWriter w(payload);
	w.Bytes(hash.data(), hash.size());
	w.U16(index);
	w.U16(static_cast<uint16_t>(count));
	w.U32(static_cast<uint32_t>(data.size()));
	w.Bytes(data.data() + offset, len);
	return true;
}

bool ReadBlobChunkHeader(ByteReader &r, BlobChunkHeader &h)
{
	r.Bytes(h.hash.data(), h.hash.size());
	h.index = r.U16();
	h.count = r.U16();
	h.total_size = r.U32();
	return r.ok();
}

BlobRef BlobStore::Put(const std::vector<uint8_t> &data)
{
	BlobRef ref = MakeBlobRef(data);
	if(!ref.Valid())
		return ref;
	complete[ref.hash] = std::make_shared<const std::vector<uint8_t>>(data);
	downloads.erase(ref.hash);
	return ref;
}

BlobData BlobStore::Get(const BlobHash &hash) const
{
	auto it = complete.find(hash);
	return it == complete.end() ? nullptr : it->second;
}

bool BlobStore::Has(const BlobRef &ref) const
{
	auto it = complete.find(ref.hash);
	return ref.Valid() && it != complete.end() && it->second->size() == ref.size;
}

void BlobStore::Want(const BlobRef &ref)
{
	if(!ref.Valid() || Has(ref) || downloads.count(ref.hash))
		return;
	if(complete.size() + downloads.size() >= kMaxEntries)
		return; // nobody has that many players; do not let a peer fill our memory
	Download d;
	d.ref = ref;
	uint32_t count = BlobChunkCount(ref.size);
	d.data.resize(ref.size);
	d.got.assign(count, false);
	d.asked_ms.assign(count, 0);
	downloads[ref.hash] = std::move(d);
}

void BlobStore::Forget(const BlobHash &hash)
{
	downloads.erase(hash);
	complete.erase(hash);
}

bool BlobStore::OnChunk(const BlobChunkHeader &h, const uint8_t *data, size_t size)
{
	auto it = downloads.find(h.hash);
	if(it == downloads.end())
		return false; // not something we asked for
	Download &d = it->second;
	if(h.total_size != d.ref.size || h.count != d.got.size() || h.index >= h.count)
		return false;
	size_t offset = static_cast<size_t>(h.index) * kBlobChunkSize;
	size_t expected = std::min(kBlobChunkSize, static_cast<size_t>(d.ref.size) - offset);
	if(size != expected || d.got[h.index])
		return false;
	std::memcpy(d.data.data() + offset, data, size);
	d.got[h.index] = true;
	d.got_count++;
	if(d.got_count < d.got.size())
		return false;

	// All there. Is it what the reference promised?
	BlobRef actual = MakeBlobRef(d.data);
	bool ok = actual == d.ref;
	if(ok)
		complete[d.ref.hash] = std::make_shared<const std::vector<uint8_t>>(std::move(d.data));
	downloads.erase(it);
	return ok;
}

void BlobStore::Poll(uint64_t now_ms, std::vector<Request> &requests, size_t max_requests, uint32_t retry_ms)
{
	requests.clear();
	for(auto &entry : downloads)
	{
		Download &d = entry.second;
		for(size_t i = 0; i < d.got.size() && requests.size() < max_requests; i++)
		{
			if(d.got[i])
				continue;
			if(d.asked_ms[i] && now_ms - d.asked_ms[i] < retry_ms)
				continue;
			d.asked_ms[i] = now_ms ? now_ms : 1;
			Request r;
			r.hash = entry.first;
			r.index = static_cast<uint16_t>(i);
			requests.push_back(r);
		}
		if(requests.size() >= max_requests)
			break;
	}
}

} // namespace coplay
