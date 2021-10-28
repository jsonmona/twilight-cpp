#include "StreamServer.h"

#include <mbedtls/sha256.h>


constexpr uint16_t SERVICE_PORT = 6495;
constexpr int32_t AUTH_PROTOCOL_VERSION = 1;


StreamServer::StreamServer() :
	log(createNamedLogger("StreamServer"))
{
	capture = CapturePipeline::createInstance();
	capture->setOutputCallback([this](DesktopFrame<ByteBuffer>&& cap) { _processOutput(std::move(cap)); });
	audioEncoder.setOnAudioData([this](const uint8_t* data, size_t len) {
		msg::Packet pkt;
		pkt.set_extra_data_len(len);
		auto audioFrame = pkt.mutable_audio_frame();
		audioFrame->set_channels(2);
		conn->send(pkt, data);
	});

	server.setOnNewConnection([this](std::unique_ptr<NetworkSocket>&& _newSock) {
		std::unique_ptr<NetworkSocket> newSock = std::move(_newSock);

		if (conn != nullptr && conn->isConnected()) {
			log->warn("Dropping new connection because current client is already connected");
			return;
		}

		conn = std::move(newSock);

		if (!conn->verifyCert()) {
			if (!doAuth_()) {
				conn->disconnect();
				return;
			}
		}

		lastStatReport = std::chrono::steady_clock::now();
		conn->setOnDisconnected([this](std::string_view msg) {
			audioEncoder.stop();
			capture->stop();
		});
		capture->start();
		audioEncoder.start();
	});
}

StreamServer::~StreamServer() {
}

void StreamServer::start() {
	server.startListen(SERVICE_PORT);
}

void StreamServer::stop() {
	server.stopListen();

	if (conn != nullptr)
		conn->disconnect();
}

void StreamServer::_processOutput(DesktopFrame<ByteBuffer>&& cap) {
	auto nowTime = std::chrono::steady_clock::now();

	if (cap.cursorPos)
		cursorPos = std::move(cap.cursorPos);

	msg::Packet pkt;
	if (cap.cursorShape) {
		msg::CursorShape* m = pkt.mutable_cursor_shape();
		m->set_width(cap.cursorShape->width);
		m->set_height(cap.cursorShape->height);
		m->set_hotspot_x(cap.cursorShape->hotspotX);
		m->set_hotspot_y(cap.cursorShape->hotspotY);

		pkt.set_extra_data_len(cap.cursorShape->image.size());
		conn->send(pkt, cap.cursorShape->image.data());
	}

	if(cursorPos && cap.desktop) {
		msg::DesktopFrame* m = pkt.mutable_desktop_frame();
		m->set_cursor_visible(cursorPos->visible);
		if (cursorPos->visible) {
			m->set_cursor_x(cursorPos->x);
			m->set_cursor_y(cursorPos->y);
		}

		pkt.set_extra_data_len(cap.desktop->size());
		conn->send(pkt, *cap.desktop);
	}

	if (nowTime - lastStatReport > std::chrono::milliseconds(250)) {
		lastStatReport = nowTime;
		auto cap = capture->calcCaptureStat();
		auto enc = capture->calcEncoderStat();

		if (cap.valid() && enc.valid()) {
			log->info("Server stat: cap={:.2f}/{:.2f}/{:.2f}  enc={:.2f}/{:.2f}/{:.2f}", cap.min * 1000, cap.avg * 1000, cap.max * 1000, enc.min * 1000, enc.avg * 1000, enc.max * 1000);
			msg::ServerPerfReport* m = pkt.mutable_server_perf_report();
			m->set_capture_min(cap.min);
			m->set_capture_avg(cap.avg);
			m->set_capture_max(cap.max);

			m->set_encoder_min(enc.min);
			m->set_encoder_avg(enc.avg);
			m->set_encoder_max(enc.max);

			pkt.set_extra_data_len(0);
			conn->send(pkt, nullptr);
		}
	}
}

// Deduplicate with StreamClient.cpp
// Returns negative on error (mbedtls error code)
static int computePin(const ByteBuffer& serverPubkey, const ByteBuffer& clientPubkey,
	const ByteBuffer& serverNonce, const ByteBuffer& clientNonce) {

	static_assert(std::numeric_limits<int>::max() > 99999999, "Int is too small to compute pin!");

	int ret;
	ByteBuffer payload;
	ByteBuffer hash(64);

	payload.reserve(serverPubkey.size() + clientPubkey.size() + serverNonce.size() + clientNonce.size());
	payload.append(serverPubkey);
	payload.append(clientPubkey);
	payload.append(serverNonce);
	payload.append(clientNonce);

	ret = mbedtls_sha256_ret(payload.data(), payload.size(), hash.data(), 0);
	if (ret < 0)
		return ret;

	uint64_t value = (uint64_t)hash[0] | (uint64_t)hash[1] << 8 |
		(uint64_t)hash[2] << 16 | (uint64_t)hash[3] << 24 |
		(uint64_t)hash[4] << 32 | (uint64_t)hash[5] << 40 |
		(uint64_t)hash[6] << 48 | (uint64_t)hash[7] << 56;

	int output = 0;
	for (int i = 0; i < 8; i++) {
		output *= 10;
		output += value % 10;
		value /= 10;
	}

	return output;
}

bool StreamServer::doAuth_() {
	int ret;
	bool status;
	msg::Packet pkt;

	ByteBuffer payload;
	ByteBuffer partialHash;
	ByteBuffer serverPubkey, clientPubkey;
	ByteBuffer nonce(16), clientNonce;

	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, nullptr, 0);
	check_quit(ret < 0, log, "Failed to seed ctr_drbg");

	ret = mbedtls_ctr_drbg_random(&ctr_drbg, nonce.data(), nonce.size());
	check_quit(ret < 0, log, "Failed to get random nonce");

	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);

	serverPubkey = server.getCert().keypair().pubkey();

	status = conn->recv(&pkt, &payload);
	if (!status || pkt.msg_case() != msg::Packet::kSignRequest)
		return false;

	auto signReq = pkt.mutable_sign_request();
	if (signReq->auth_protocol_version() != AUTH_PROTOCOL_VERSION) {
		auto signRes = pkt.mutable_sign_response();
		*signRes->mutable_error_msg() = "Version mismatch!"; // TODO: Translate
		signRes->set_status(msg::SignResponse_AuthStatus_VERSION_MISMATCH_ERROR);
		conn->send(pkt, nullptr);
		return false;
	}
	std::string clientHostname = *signReq->mutable_hostname();
	partialHash.append(payload.data(), 48);
	clientPubkey.append(payload.data() + 48, payload.size() - 48);

	pkt.mutable_server_nonce_notify();
	pkt.set_extra_data_len(nonce.size());
	status = conn->send(pkt, nonce);
	if (!status)
		return false;

	status = conn->recv(&pkt, &clientNonce);
	if (!status || pkt.msg_case() != msg::Packet::kClientNonceNotify)
		return false;

	// verify the partial hash
	payload.resize(0); //FIXME: Deallocating
	payload.reserve(serverPubkey.size() + clientPubkey.size() + clientNonce.size() + 64);
	payload.append(serverPubkey);
	payload.append(clientPubkey);
	payload.append(clientNonce);
	ret = mbedtls_sha512_ret(payload.data(), payload.size(), payload.data() + payload.size(), 1);
	check_quit(ret < 0, log, "Failed to compute partial hash: {}", interpretMbedtlsError(ret));

	if (memcmp(partialHash.data(), payload.data() + payload.size(), 48) != 0) {
		log->warn("Partial hash mismatch! Server might be under attack!");
		return false;
	}

	int pin = computePin(serverPubkey, clientPubkey, nonce, clientNonce);
	if (pin < 0) {
		log->error("Failed to compute pin: {}", interpretMbedtlsError(pin));
		return false;
	}

	pkt.mutable_server_pin_ready();
	pkt.set_extra_data_len(0);
	status = conn->send(pkt, nullptr);
	if (!status)
		return false;

	//FIXME: Using standard input
	printf("Auth requested. Enter pin (-1 to cancel): ");
	char buf[256];
	fgets(buf, sizeof(buf), stdin);
	int enteredPin = 0;
	for (int i = 0; i < 256 && buf[i] != '\0'; i++) {
		if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r')
			continue;
		if (buf[i] < '0' || '9' < buf[i]) {
			enteredPin = -1;
			break;
		}
		enteredPin = enteredPin * 10 + (buf[i] - '0');
	}

	if (pin != enteredPin) {
		log->warn("Pin enter mismatch: Expected {}, got {}", pin, enteredPin);

		auto* signResponse = pkt.mutable_sign_response();
		signResponse->set_status(msg::SignResponse_AuthStatus_PIN_ERROR);
		pkt.set_extra_data_len(0);
		conn->send(pkt, nullptr);

		return false;
	}

	Keypair clientPubkeyCtx;
	status = clientPubkeyCtx.parsePubkey(clientPubkey);
	check_quit(!status, log, "Failed to parse client pubkey");

	std::string subjectName = "O=daylight,OU=";
	subjectName += "hash";
	subjectName += ",CN=";
	subjectName += clientHostname;  //TODO: Validate hostname

	payload = server.getCert().signCert(subjectName.c_str(), clientPubkeyCtx.pk());

	auto* signResponse = pkt.mutable_sign_response();
	signResponse->set_status(msg::SignResponse_AuthStatus_SUCCESS);
	pkt.set_extra_data_len(payload.size());
	status = conn->send(pkt, payload);
	if (!status)
		return false;

	return true;
}
