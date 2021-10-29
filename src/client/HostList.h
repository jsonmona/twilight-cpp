#ifndef DAYLIGHT_CLIENT_HOST_LIST_H_
#define DAYLIGHT_CLIENT_HOST_LIST_H_


#include "common/log.h"

#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include <toml.hpp>

#include <chrono>
#include <string>
#include <memory>
#include <vector>


class HostList {
public:
	struct Entry {
		std::string nickname; //< Display name
		std::vector<std::string> addr; //< Addresses in preference order
		std::chrono::system_clock::time_point lastConnected; //< Last connected time
		mbedtls_x509_crt serverCert; //< Server's self-signed certificate (CA)
		mbedtls_x509_crt clientCert; //< A certificate issued by server

		Entry();
		Entry(const Entry& copy) = delete;
		Entry(Entry&& move) = delete;
		~Entry();

		bool hasConnected() const;
		bool hasServerCert() const;
		bool hasClientCert() const;
		void updateLastConnected();
	};

	HostList();
	HostList(const HostList& copy) = delete;
	HostList(HostList&& move) = default;

	bool load(toml::array hosts);
	bool loadFromFile(const char* filename);

	bool save(std::ostream& out);
	bool saveToFile(const char* filename);

	std::vector<std::shared_ptr<Entry>> hosts;

private:

	LoggerPtr log;
};


using HostListEntry = std::shared_ptr<HostList::Entry>;


#endif