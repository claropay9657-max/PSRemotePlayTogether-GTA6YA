// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/web.h>

#ifndef COPLAY_WEB

namespace coplay {

bool WebGuestsSupported()
{
	return false;
}

std::unique_ptr<WebGateway> CreateWebGateway(const WebConfig &, const WebGateway::Callbacks &)
{
	return nullptr;
}

} // namespace coplay

#else

/*
 * The gateway built on libdatachannel (https://github.com/paullouisageneau/libdatachannel,
 * MPL 2.0): one PeerConnection per browser, each with the one data channel the page opens.
 *
 * Threads: libdatachannel calls back from threads of its own, sometimes while it
 * holds locks of its own. Those callbacks therefore only put a note into a queue.
 * One thread of the gateway takes the notes out and tells the host, so the host
 * may call back into the gateway from there without any danger of two threads
 * waiting for each other.
 */

#include <coplay/protocol.h>

#include <rtc/rtc.hpp>

#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace coplay {

/**
 * Large enough for the largest packet of protocol.h to travel in one piece: the
 * library takes 108 bytes off for what goes around a message, another 28 are the
 * message's own wrapping. What actually goes over the wire stays below 1320 bytes.
 */
static const size_t kWebMtu = 1350;
/** Changes to an answer are collected for this long before the host is told. */
static const uint32_t kAnswerSettleMs = 120;
static const size_t kMaxQueuedEvents = 20000;

bool WebGuestsSupported()
{
	return true;
}

static std::string BioToString(BIO *bio)
{
	char *data = nullptr;
	long size = BIO_get_mem_data(bio, &data);
	return size > 0 && data ? std::string(data, static_cast<size_t>(size)) : std::string();
}

/**
 * The certificate every browser of this session gets to see. Made here, and not by
 * the library for each connection, so that the host's note can name one fingerprint.
 */
static bool MakeCertificate(std::string &cert_pem, std::string &key_pem,
		std::array<uint8_t, kWebFingerprintSize> &fingerprint)
{
	bool ok = false;
	EVP_PKEY *key = nullptr;
	X509 *cert = nullptr;
	BIO *cert_bio = nullptr;
	BIO *key_bio = nullptr;
	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
	do
	{
		if(!ctx || EVP_PKEY_keygen_init(ctx) != 1)
			break;
		if(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) != 1)
			break;
		if(EVP_PKEY_keygen(ctx, &key) != 1 || !key)
			break;
		cert = X509_new();
		if(!cert)
			break;
		uint8_t serial[8];
		if(!RandomBytes(serial, sizeof(serial)))
			break;
		serial[0] &= 0x7f;
		BIGNUM *bn = BN_bin2bn(serial, sizeof(serial), nullptr);
		if(!bn)
			break;
		BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(cert));
		BN_free(bn);
		X509_set_version(cert, 2);
		// A day back for clocks that are behind; no session lasts a year
		X509_gmtime_adj(X509_getm_notBefore(cert), -24 * 3600);
		X509_gmtime_adj(X509_getm_notAfter(cert), 365L * 24 * 3600);
		if(X509_set_pubkey(cert, key) != 1)
			break;
		X509_NAME *name = X509_get_subject_name(cert);
		X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
			reinterpret_cast<const unsigned char *>("PSRemotePlayTogether"), -1, -1, 0);
		X509_set_issuer_name(cert, name);
		if(!X509_sign(cert, key, EVP_sha256()))
			break;
		unsigned int md_size = 0;
		uint8_t md[EVP_MAX_MD_SIZE];
		if(X509_digest(cert, EVP_sha256(), md, &md_size) != 1 || md_size != fingerprint.size())
			break;
		std::copy(md, md + md_size, fingerprint.begin());
		cert_bio = BIO_new(BIO_s_mem());
		key_bio = BIO_new(BIO_s_mem());
		if(!cert_bio || !key_bio)
			break;
		if(PEM_write_bio_X509(cert_bio, cert) != 1)
			break;
		if(PEM_write_bio_PrivateKey(key_bio, key, nullptr, nullptr, 0, nullptr, nullptr) != 1)
			break;
		cert_pem = BioToString(cert_bio);
		key_pem = BioToString(key_bio);
		ok = !cert_pem.empty() && !key_pem.empty();
	} while(false);
	BIO_free(cert_bio);
	BIO_free(key_bio);
	X509_free(cert);
	EVP_PKEY_free(key);
	EVP_PKEY_CTX_free(ctx);
	return ok;
}

namespace {

struct Event
{
	enum class Kind { Open, Message, Closed, Answers, Log };
	Kind kind = Kind::Log;
	uint64_t peer_id = 0;
	std::vector<uint8_t> data;
	std::string text;
};

struct Peer
{
	uint64_t id = 0;
	std::shared_ptr<rtc::PeerConnection> pc;
	std::shared_ptr<rtc::DataChannel> channel;
	WebAnswer answer;
	bool described = false; // the library has said under which name and password it answers
	bool open = false;
	bool gone = false;      // on_closed was reported, or Close() was called
	uint64_t created_ms = 0;
};

/** What the library's callbacks may touch. Lives as long as any of them can still be called. */
struct Shared
{
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<Event> events;
	std::map<uint64_t, std::shared_ptr<Peer>> peers;
	/** Connections that are done with; they are let go of on the gateway's thread. */
	std::vector<std::shared_ptr<Peer>> trash;
	bool answers_dirty = false;
	uint64_t answers_dirty_ms = 0;
	bool stop = false;

	void Push(Event &&e)
	{
		// with mutex
		if(events.size() < kMaxQueuedEvents)
			events.push_back(std::move(e));
		wake.notify_all();
	}
};

class RtcGateway : public WebGateway
{
	public:
		RtcGateway(const WebConfig &config, const Callbacks &callbacks)
			: config(config), callbacks(callbacks), shared(std::make_shared<Shared>())
		{
			// For looking at what the library does: PSRPT_WEBRTC_LOG=debug (or verbose, info, warning)
			if(const char *level = std::getenv("PSRPT_WEBRTC_LOG"))
			{
				std::string l = level;
				rtc::InitLogger(l == "verbose" ? rtc::LogLevel::Verbose : l == "debug" ? rtc::LogLevel::Debug
					: l == "info" ? rtc::LogLevel::Info : rtc::LogLevel::Warning);
			}
			// The library's settings for the connection underneath a data channel, which count
			// for every connection made after this. A frame of video is a burst of up to a few
			// hundred packets: it should go out in one go and not wait for the first ones to be
			// confirmed. And what cannot go out should show up as a backlog soon (Buffered()).
			rtc::SctpSettings sctp;
			sctp.sendBufferSize = 256 * 1024;
			sctp.initialCongestionWindow = 64;
			sctp.maxBurst = 64;
			rtc::SetSctpSettings(sctp);
			usable = MakeCertificate(cert_pem, key_pem, fingerprint);
			if(!usable)
			{
				Event e;
				e.kind = Event::Kind::Log;
				e.text = "No certificate could be made for connections to web browsers: friends cannot join from one";
				shared->events.push_back(std::move(e)); // nobody else is there yet
			}
			thread = std::thread(&RtcGateway::Run, this);
		}

		~RtcGateway() override
		{
			std::vector<std::shared_ptr<Peer>> all;
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				shared->stop = true;
				for(auto &entry : shared->peers)
					all.push_back(entry.second);
				shared->peers.clear();
				all.insert(all.end(), shared->trash.begin(), shared->trash.end());
				shared->trash.clear();
				shared->wake.notify_all();
			}
			if(thread.joinable())
				thread.join();
			for(auto &peer : all)
				Release(*peer);
		}

		void Offer(const WebOffer &offer) override
		{
			if(!usable || !offer.peer_id)
				return;
			auto peer = std::make_shared<Peer>();
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				if(shared->stop || shared->peers.count(offer.peer_id))
					return; // this browser is being taken care of
				size_t pending = 0;
				for(auto &entry : shared->peers)
					pending += entry.second->open ? 0 : 1;
				if(pending >= kWebMaxPending)
					return; // it asks again in a moment
				peer->id = offer.peer_id;
				peer->answer.peer_id = offer.peer_id;
				peer->created_ms = NowMs();
				shared->peers[offer.peer_id] = peer;
			}

			if(config.port != 0 && !port_checked)
			{
				// The library says nothing when the port all browsers are to share is in use
				// by something else on this computer; the connection just never comes to be.
				// So look first, once, before the library takes the port for itself.
				port_checked = true;
				UdpSocket probe;
				if(!probe.Bind(config.port, nullptr))
				{
					port_taken = true;
					std::lock_guard<std::mutex> lock(shared->mutex);
					Event e;
					e.kind = Event::Kind::Log;
					e.text = "UDP port " + std::to_string(config.port)
						+ " is in use on this computer: each browser gets a port of its own instead";
					shared->Push(std::move(e));
				}
				probe.Close();
			}
			std::string problem = Connect(offer, peer, config.port != 0 && !port_taken);
			if(!problem.empty() && config.port != 0 && !port_taken)
			{
				// Most likely the shared port is in use by something else on this computer.
				// A port of its own for every browser works without it.
				port_taken = true;
				std::shared_ptr<rtc::PeerConnection> failed;
				{
					std::lock_guard<std::mutex> lock(shared->mutex);
					Event e;
					e.kind = Event::Kind::Log;
					e.text = "Browsers cannot share UDP port " + std::to_string(config.port) + " (" + problem
						+ "): each one gets a port of its own instead";
					shared->Push(std::move(e));
					failed.swap(peer->pc);
				}
				// Let go of it without the lock: closing it calls back into this gateway
				if(failed)
				{
					try
					{
						failed->resetCallbacks();
						failed->close();
					}
					catch(const std::exception &)
					{
					}
					failed.reset();
				}
				problem = Connect(offer, peer, false);
			}
			if(!problem.empty())
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				shared->peers.erase(offer.peer_id);
				shared->trash.push_back(peer);
				Event e;
				e.kind = Event::Kind::Log;
				e.text = "A browser could not be let in: " + problem;
				shared->Push(std::move(e));
			}
		}

		/** @return empty if the connection is on its way, otherwise what went wrong */
		std::string Connect(const WebOffer &offer, const std::shared_ptr<Peer> &peer, bool shared_port)
		{
			std::string problem;
			try
			{
				rtc::Configuration rc;
				for(const StunServer &s : config.stun_servers)
				{
					if(!s.host.empty())
						rc.iceServers.emplace_back(s.host, s.port);
				}
				rc.certificatePemFile = cert_pem;
				rc.keyPemFile = key_pem;
				rc.mtu = kWebMtu;
				if(shared_port)
				{
					rc.portRangeBegin = config.port;
					rc.portRangeEnd = config.port;
					rc.enableIceUdpMux = true;
				}
				auto pc = std::make_shared<rtc::PeerConnection>(rc);
				std::weak_ptr<Shared> weak = shared;
				uint64_t id = offer.peer_id;

				pc->onLocalDescription([weak, id](rtc::Description description) {
					auto s = weak.lock();
					if(!s)
						return;
					std::lock_guard<std::mutex> lock(s->mutex);
					auto it = s->peers.find(id);
					if(it == s->peers.end())
						return;
					Peer &p = *it->second;
					p.answer.side.ufrag = description.iceUfrag().value_or(std::string());
					p.answer.side.pwd = description.icePwd().value_or(std::string());
					p.answer.active = description.role() != rtc::Description::Role::Passive;
					p.described = !p.answer.side.ufrag.empty() && !p.answer.side.pwd.empty();
					for(const rtc::Candidate &c : description.candidates())
						AddCandidate(p, c);
					MarkDirty(*s);
				});
				pc->onLocalCandidate([weak, id](rtc::Candidate candidate) {
					auto s = weak.lock();
					if(!s)
						return;
					std::lock_guard<std::mutex> lock(s->mutex);
					auto it = s->peers.find(id);
					if(it == s->peers.end())
						return;
					if(AddCandidate(*it->second, candidate))
						MarkDirty(*s);
				});
				pc->onStateChange([weak, id](rtc::PeerConnection::State state) {
					if(state != rtc::PeerConnection::State::Failed && state != rtc::PeerConnection::State::Closed
						&& state != rtc::PeerConnection::State::Disconnected)
						return;
					auto s = weak.lock();
					if(!s)
						return;
					std::lock_guard<std::mutex> lock(s->mutex);
					Event e;
					e.kind = Event::Kind::Closed;
					e.peer_id = id;
					s->Push(std::move(e));
				});
				pc->onDataChannel([weak, id](std::shared_ptr<rtc::DataChannel> channel) {
					auto s = weak.lock();
					if(!s)
						return;
					channel->onOpen([weak, id]() {
						auto s2 = weak.lock();
						if(!s2)
							return;
						std::lock_guard<std::mutex> lock(s2->mutex);
						Event e;
						e.kind = Event::Kind::Open;
						e.peer_id = id;
						s2->Push(std::move(e));
					});
					channel->onClosed([weak, id]() {
						auto s2 = weak.lock();
						if(!s2)
							return;
						std::lock_guard<std::mutex> lock(s2->mutex);
						Event e;
						e.kind = Event::Kind::Closed;
						e.peer_id = id;
						s2->Push(std::move(e));
					});
					channel->onMessage([weak, id](rtc::message_variant message) {
						const rtc::binary *data = std::get_if<rtc::binary>(&message);
						if(!data || data->empty() || data->size() > kMaxDatagram)
							return; // the page only sends binary messages of a packet's size
						auto s2 = weak.lock();
						if(!s2)
							return;
						Event e;
						e.kind = Event::Kind::Message;
						e.peer_id = id;
						const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data->data());
						e.data.assign(bytes, bytes + data->size());
						std::lock_guard<std::mutex> lock(s2->mutex);
						s2->Push(std::move(e));
					});
					bool open_already = channel->isOpen();
					std::lock_guard<std::mutex> lock(s->mutex);
					auto it = s->peers.find(id);
					if(it == s->peers.end() || it->second->channel)
						return; // one channel per browser
					it->second->channel = channel;
					if(open_already)
					{
						Event e;
						e.kind = Event::Kind::Open;
						e.peer_id = id;
						s->Push(std::move(e));
					}
				});

				{
					std::lock_guard<std::mutex> lock(shared->mutex);
					peer->pc = pc;
				}
				// Answers by itself, and starts looking for this side's addresses
				pc->setRemoteDescription(rtc::Description(WebOfferToSdp(offer), rtc::Description::Type::Offer));
			}
			catch(const std::exception &e)
			{
				problem = e.what();
				if(problem.empty())
					problem = "unknown error";
			}
			return problem;
		}

		WebAnswers Answers() override
		{
			WebAnswers out;
			out.protocol = kProtocolVersion;
			out.fingerprint = fingerprint;
			std::lock_guard<std::mutex> lock(shared->mutex);
			for(auto &entry : shared->peers)
			{
				const Peer &p = *entry.second;
				if(p.open || p.gone || !p.described || p.answer.side.candidates.empty())
					continue;
				if(out.answers.size() < kWebMaxPending)
					out.answers.push_back(p.answer);
			}
			return out;
		}

		bool Send(uint64_t peer_id, const uint8_t *data, size_t size) override
		{
			std::shared_ptr<rtc::DataChannel> channel;
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				auto it = shared->peers.find(peer_id);
				if(it == shared->peers.end() || !it->second->open || it->second->gone)
					return false;
				channel = it->second->channel;
			}
			if(!channel)
				return false;
			try
			{
				// false only means "not on the wire yet"
				channel->send(reinterpret_cast<const rtc::byte *>(data), size);
				return true;
			}
			catch(const std::exception &)
			{
				return false;
			}
		}

		size_t Buffered(uint64_t peer_id) override
		{
			std::shared_ptr<rtc::DataChannel> channel;
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				auto it = shared->peers.find(peer_id);
				if(it == shared->peers.end())
					return 0;
				channel = it->second->channel;
			}
			try
			{
				return channel ? channel->bufferedAmount() : 0;
			}
			catch(const std::exception &)
			{
				return 0;
			}
		}

		void Close(uint64_t peer_id) override
		{
			std::lock_guard<std::mutex> lock(shared->mutex);
			auto it = shared->peers.find(peer_id);
			if(it == shared->peers.end())
				return;
			it->second->gone = true;
			shared->trash.push_back(it->second);
			shared->peers.erase(it);
			MarkDirty(*shared);
		}

		std::string PeerAddress(uint64_t peer_id) override
		{
			std::shared_ptr<rtc::PeerConnection> pc;
			{
				std::lock_guard<std::mutex> lock(shared->mutex);
				auto it = shared->peers.find(peer_id);
				if(it == shared->peers.end())
					return std::string();
				pc = it->second->pc;
			}
			try
			{
				return pc ? pc->remoteAddress().value_or(std::string()) : std::string();
			}
			catch(const std::exception &)
			{
				return std::string();
			}
		}

	private:
		static void MarkDirty(Shared &s)
		{
			// with mutex
			if(!s.answers_dirty)
			{
				s.answers_dirty = true;
				s.answers_dirty_ms = NowMs();
			}
			s.wake.notify_all();
		}

		/** @return true if the candidate is one to tell the browser about and was not known yet */
		static bool AddCandidate(Peer &p, const rtc::Candidate &candidate)
		{
			// with mutex
			if(candidate.transportType() != rtc::Candidate::TransportType::Udp || !candidate.isResolved())
				return false;
			WebCandidate c;
			if(candidate.type() == rtc::Candidate::Type::Host)
				c.type = WebCandidateType::Host;
			else if(candidate.type() == rtc::Candidate::Type::ServerReflexive)
				c.type = WebCandidateType::Reflexive;
			else
				return false;
			auto address = candidate.address();
			auto port = candidate.port();
			if(!address || !port || !ParseAddress(*address, *port, c.address))
				return false;
			for(const WebCandidate &known : p.answer.side.candidates)
			{
				if(known == c)
					return false;
			}
			if(p.answer.side.candidates.size() >= kWebMaxCandidates)
				return false;
			// The address on the internet first: a note that gets too long loses its end
			if(c.type == WebCandidateType::Reflexive)
				p.answer.side.candidates.insert(p.answer.side.candidates.begin(), c);
			else
				p.answer.side.candidates.push_back(c);
			return true;
		}

		static void Release(Peer &peer)
		{
			// Without any lock: the library may want to finish a callback first
			std::shared_ptr<rtc::DataChannel> channel;
			std::shared_ptr<rtc::PeerConnection> pc;
			channel.swap(peer.channel);
			pc.swap(peer.pc);
			try
			{
				if(channel)
				{
					channel->resetCallbacks();
					channel->close();
				}
				if(pc)
				{
					pc->resetCallbacks();
					pc->close();
				}
			}
			catch(const std::exception &)
			{
			}
		}

		void Run()
		{
			std::unique_lock<std::mutex> lock(shared->mutex);
			while(!shared->stop)
			{
				uint64_t now = NowMs();

				// Browsers that never arrived
				std::vector<uint64_t> late;
				for(auto &entry : shared->peers)
				{
					Peer &p = *entry.second;
					if(!p.open && !p.gone && ElapsedMs(now, p.created_ms) > config.connect_timeout_ms)
						late.push_back(entry.first);
				}
				for(uint64_t id : late)
				{
					Event e;
					e.kind = Event::Kind::Closed;
					e.peer_id = id;
					shared->Push(std::move(e));
				}

				if(shared->answers_dirty && ElapsedMs(now, shared->answers_dirty_ms) >= kAnswerSettleMs)
				{
					shared->answers_dirty = false;
					Event e;
					e.kind = Event::Kind::Answers;
					shared->Push(std::move(e));
				}

				std::deque<Event> events;
				events.swap(shared->events);
				std::vector<std::shared_ptr<Peer>> trash;
				trash.swap(shared->trash);
				if(events.empty() && trash.empty())
				{
					shared->wake.wait_for(lock, std::chrono::milliseconds(shared->answers_dirty ? 30 : 250));
					continue;
				}

				// Decide under the lock what each note means, tell the host without it
				struct Call
				{
					Event event;
					bool deliver = false;
				};
				std::vector<Call> calls;
				calls.reserve(events.size());
				for(Event &e : events)
				{
					Call c;
					c.deliver = true;
					auto it = shared->peers.find(e.peer_id);
					if(e.kind == Event::Kind::Open)
					{
						if(it == shared->peers.end() || it->second->open || it->second->gone || !it->second->channel)
							c.deliver = false;
						else
						{
							it->second->open = true;
							MarkDirty(*shared); // its answer is not needed any more
						}
					}
					else if(e.kind == Event::Kind::Message)
						c.deliver = it != shared->peers.end() && it->second->open && !it->second->gone;
					else if(e.kind == Event::Kind::Closed)
					{
						if(it == shared->peers.end() || it->second->gone)
							c.deliver = false;
						else
						{
							it->second->gone = true;
							trash.push_back(it->second);
							shared->peers.erase(it);
							MarkDirty(*shared);
						}
					}
					c.event = std::move(e);
					calls.push_back(std::move(c));
				}

				lock.unlock();
				for(Call &c : calls)
				{
					if(!c.deliver)
						continue;
					switch(c.event.kind)
					{
						case Event::Kind::Open:
							if(callbacks.on_open)
								callbacks.on_open(c.event.peer_id);
							break;
						case Event::Kind::Message:
							if(callbacks.on_message)
								callbacks.on_message(c.event.peer_id, c.event.data.data(), c.event.data.size());
							break;
						case Event::Kind::Closed:
							if(callbacks.on_closed)
								callbacks.on_closed(c.event.peer_id);
							break;
						case Event::Kind::Answers:
							if(callbacks.on_answers_changed)
								callbacks.on_answers_changed();
							break;
						case Event::Kind::Log:
							if(callbacks.on_log)
								callbacks.on_log(c.event.text);
							break;
					}
				}
				for(auto &peer : trash)
					Release(*peer);
				trash.clear();
				lock.lock();
			}
		}

		const WebConfig config;
		const Callbacks callbacks;
		std::shared_ptr<Shared> shared;
		std::thread thread;
		bool usable = false;
		/** The shared port could not be had; only touched by Offer(), which one thread calls. */
		bool port_taken = false;
		bool port_checked = false;
		std::string cert_pem;
		std::string key_pem;
		std::array<uint8_t, kWebFingerprintSize> fingerprint{};
};

} // namespace

std::unique_ptr<WebGateway> CreateWebGateway(const WebConfig &config, const WebGateway::Callbacks &callbacks)
{
	return std::make_unique<RtcGateway>(config, callbacks);
}

} // namespace coplay

#endif // COPLAY_WEB
