/**
 * libdatachannel media sender example
 * Copyright (c) 2020 Staz Modrzynski
 * Copyright (c) 2020 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "rtc/rtc.hpp"

#include <cstddef>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
typedef int SOCKET;
#endif

using nlohmann::json;
using namespace std::chrono_literals;

const int BUFFER_SIZE = 2048;

int main(int argc, char **argv) {
	try {
		rtc::InitLogger(rtc::LogLevel::Debug);

		std::string signalingHost = "127.0.0.1";
		uint16_t signalingPort = 8000;
		std::string localId = "sender";
		std::string remoteId = "browser";

		for (int i = 1; i < argc; ++i) {
			std::string arg = argv[i];
			if (arg == "--signaling-ip" && i + 1 < argc) {
				signalingHost = argv[++i];
			} else if (arg == "--signaling-port" && i + 1 < argc) {
				signalingPort = static_cast<uint16_t>(std::stoi(argv[++i]));
			} else if (arg == "--local-id" && i + 1 < argc) {
				localId = argv[++i];
			} else if (arg == "--remote-id" && i + 1 < argc) {
				remoteId = argv[++i];
			} else if (arg == "--help") {
				std::cout << "usage: media-sender [--signaling-ip IP] [--signaling-port PORT] "
				             "[--local-id ID] [--remote-id ID]\n";
				return 0;
			} else {
				std::cerr << "Unknown argument: " << arg << std::endl;
				return 1;
			}
		}

		auto ws = std::make_shared<rtc::WebSocket>();
		bool wsOpen = false;
		std::string pendingOffer;
		std::mutex pcMutex;
		std::shared_ptr<rtc::PeerConnection> pc;
		std::shared_ptr<rtc::Track> track;
		std::atomic<bool> idle{true};
		std::atomic<int64_t> lastPacketMs{0};
		std::atomic<bool> reconnecting{false};

		ws->onOpen([&]() {
			wsOpen = true;
			std::cout << "WebSocket connected, signaling ready" << std::endl;
		});

		ws->onError([](const std::string &error) {
			std::cout << "WebSocket failed: " << error << std::endl;
		});

		ws->onClosed([&]() {
			wsOpen = false;
			std::cout << "WebSocket closed" << std::endl;
		});

		// Handle signaling messages (request/answer)
		ws->onMessage([&](rtc::message_variant data) {
			if (!std::holds_alternative<std::string>(data))
				return;

			json message = json::parse(std::get<std::string>(data), nullptr, false);
			if (message.is_discarded())
				return;

			const auto typeIt = message.find("type");
			if (typeIt == message.end())
				return;
			const std::string type = typeIt->get<std::string>();

			if (type == "answer") {
				const auto sdpIt = message.find("sdp");
				if (sdpIt == message.end())
					return;
				std::shared_ptr<rtc::PeerConnection> currentPc;
				{
					std::lock_guard<std::mutex> lock(pcMutex);
					currentPc = pc;
				}
				if (currentPc) {
					rtc::Description answer(sdpIt->get<std::string>(), "answer");
					currentPc->setRemoteDescription(answer);
					std::cout << "Applied remote answer" << std::endl;
				}
			} else if (type == "request" || type == "ready") {
				if (!pendingOffer.empty() && wsOpen) {
					json offerMessage = {{"id", remoteId}, {"type", "offer"}, {"sdp", pendingOffer}};
					ws->send(offerMessage.dump());
					std::cout << "Sent offer to " << remoteId << std::endl;
				}
			}
		});

		const std::string url = "ws://" + signalingHost + ":" + std::to_string(signalingPort) +
		                        "/" + localId;
		std::cout << "WebSocket URL is " << url << std::endl;
		ws->open(url);

		std::cout << "Waiting for signaling to be connected..." << std::endl;
		while (!wsOpen) {
			if (ws->isClosed())
				throw std::runtime_error("WebSocket closed before connection");
			std::this_thread::sleep_for(100ms);
		}

		const rtc::SSRC ssrc = 42;
		auto createPeerConnection = [&]() {
			reconnecting = true;
			auto newPc = std::make_shared<rtc::PeerConnection>();

			newPc->onStateChange([](rtc::PeerConnection::State state) {
				std::cout << "State: " << state << std::endl;
			});

			newPc->onGatheringStateChange(
			    [newPc, ws, &pendingOffer, &wsOpen, &remoteId](rtc::PeerConnection::GatheringState state) {
				    std::cout << "Gathering State: " << state << std::endl;
				    if (state == rtc::PeerConnection::GatheringState::Complete) {
					    auto description = newPc->localDescription();
					    json message = {{"type", description->typeString()},
					                    {"sdp", std::string(description.value())}};
					    std::cout << "Local description ready" << std::endl;
					    pendingOffer = message["sdp"].get<std::string>();
					    if (wsOpen) {
						    json offerMessage = {{"id", remoteId},
						                         {"type", "offer"},
						                         {"sdp", pendingOffer}};
						    ws->send(offerMessage.dump());
						    std::cout << "Sent offer to " << remoteId << std::endl;
					    }
				    }
			    });

			rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
			media.addH264Codec(96); // Must match the payload type of the external h264 RTP stream
			media.addSSRC(ssrc, "video-send");
			auto newTrack = newPc->addTrack(media);

			newPc->setLocalDescription();

			{
				std::lock_guard<std::mutex> lock(pcMutex);
				if (pc)
					pc->close();
				pc = newPc;
				track = newTrack;
			}
			reconnecting = false;
		};

		SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
		struct sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		addr.sin_port = htons(6000);

		if (bind(sock, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0)
			throw std::runtime_error("Failed to bind UDP socket on 127.0.0.1:6000");

		int rcvBufSize = 212992;
		setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&rcvBufSize),
		           sizeof(rcvBufSize));

		createPeerConnection();

		std::cout << "RTP video stream expected on localhost:6000" << std::endl;
		std::cout << "Waiting for answer via signaling..." << std::endl;

		std::thread watchdog([&]() {
			const int64_t idleThresholdMs = 2000;
			while (true) {
				if (!wsOpen || reconnecting)
					std::this_thread::sleep_for(200ms);
				int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				                    std::chrono::steady_clock::now().time_since_epoch())
				                    .count();
				int64_t lastMs = lastPacketMs.load();
				if (lastMs != 0 && (nowMs - lastMs) > idleThresholdMs && !idle.load()) {
					std::cout << "RTP idle detected, renegotiating..." << std::endl;
					idle = true;
					pendingOffer.clear();
					createPeerConnection();
				}
				std::this_thread::sleep_for(200ms);
			}
		});
		watchdog.detach();

		// Receive from UDP (keep running even if the sender stops/restarts)
		char buffer[BUFFER_SIZE];
		while (true) {
			int len = recv(sock, buffer, BUFFER_SIZE, 0);
			if (len < 0) {
				if (errno == EINTR)
					continue;
				std::cerr << "recv failed: " << std::strerror(errno) << std::endl;
				std::this_thread::sleep_for(200ms);
				continue;
			}

			std::shared_ptr<rtc::Track> currentTrack;
			{
				std::lock_guard<std::mutex> lock(pcMutex);
				currentTrack = track;
			}

			if (!currentTrack || len < static_cast<int>(sizeof(rtc::RtpHeader)) ||
			    !currentTrack->isOpen())
				continue;

			lastPacketMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			                   std::chrono::steady_clock::now().time_since_epoch())
			                   .count();
			idle = false;

			auto rtp = reinterpret_cast<rtc::RtpHeader *>(buffer);
			rtp->setSsrc(ssrc);

			try {
				currentTrack->send(reinterpret_cast<const std::byte *>(buffer), len);
			} catch (const std::exception &e) {
				std::cerr << "Track send error: " << e.what() << std::endl;
				pendingOffer.clear();
				createPeerConnection();
			}
		}

	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}
}
