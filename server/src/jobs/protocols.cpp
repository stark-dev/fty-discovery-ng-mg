/*  =========================================================================
    discover.cpp - Protocols discovery job

    Copyright (C) 2014 - 2020 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
 */

#include "protocols.h"
#include "commands.h"
#include "common.h"
#include "message-bus.h"
#include "protocols/ping.h"
#include "protocols/xml-pdc.h"
#include <fty/split.h>
#include <fty_log.h>
#include <set>

namespace fty::job {

// =====================================================================================================================

namespace response {

    /// Response wrapper
    class Protocols : public BasicResponse<Protocols>
    {
    public:
        commands::protocols::Out protocols = FIELD("protocols");

    public:
        using BasicResponse::BasicResponse;
        META_BASE(Protocols, BasicResponse<Protocols>, protocols);

    public:
        const commands::protocols::Out& data()
        {
            return protocols;
        }
    };

} // namespace response
// =====================================================================================================================

Protocols::Protocols(const Message& in, MessageBus& bus)
    : m_in(in)
    , m_bus(&bus)
{
}

void Protocols::operator()()
{
    response::Protocols response;

    if (m_in.userData.empty()) {
        response.setError("Wrong input data");
        if (auto res = m_bus->reply(fty::Channel, m_in, response); !res) {
            log_error(res.error().c_str());
        }
        return;
    }

    Expected<commands::protocols::In> cmd = m_in.userData.decode<commands::protocols::In>();
    if (!cmd) {
        response.setError("Wrong input data");
        if (auto res = m_bus->reply(fty::Channel, m_in, response); !res) {
            log_error(res.error().c_str());
        }
        return;
    }

    if (cmd->address == "__fake__") {
        response.protocols.setValue({"NUT_SNMP", "NUT_XML_PDC"});
        response.status = Message::Status::Ok;
        if (auto res = m_bus->reply(fty::Channel, m_in, response); !res) {
            log_error(res.error().c_str());
        }
        return;
    }

    log_error("check addr %s", cmd->address.value().c_str());
    if (!available(cmd->address)) {
        response.setError("Host is not available");
        if (auto res = m_bus->reply(fty::Channel, m_in, response); !res) {
            log_error(res.error().c_str());
        }
        return;
    }

    std::vector<BasicInfo> protocols;

    if (auto res = tryXmlPdc(cmd->address)) {
        protocols.emplace_back(*res);
        log_info("Found XML  device: '%s' mibs[]", res->name.value().c_str());
    } else {
        log_error(res.error().c_str());
    }

    if (auto res = trySnmp(cmd->address, 161, "public", "")) {
        protocols.emplace_back(*res);
        log_info("Found SNMP device: '%s' mibs[%s]", res->name.value().c_str(), implode(res->mibs, ", ").c_str());
    } else {
        log_error(res.error().c_str());
    }

    sortProtocols(protocols);

    for (const auto& prot : protocols) {
        switch (prot.type) {
            case BasicInfo::Type::Snmp:
                response.protocols.append("NUT_SNMP");
                break;
            case BasicInfo::Type::Xml:
                response.protocols.append("NUT_XML_PDC");
                break;
        }
    }

    response.status = Message::Status::Ok;
    if (auto res = m_bus->reply(fty::Channel, m_in, response); !res) {
        log_error(res.error().c_str());
    }
}

Expected<BasicInfo> Protocols::tryXmlPdc(const std::string& ipAddress) const
{
    protocol::XmlPdc xml(ipAddress);
    if (auto prod = xml.get<protocol::ProductInfo>("product.xml")) {
        if (auto props = xml.get<protocol::Properties>(prod->summary.summary.url)) {
            BasicInfo info;
            if (auto res = props->value("UPS.PowerSummary.iProduct")) {
                info.name = *res;
            }
            if (auto res = props->value("UPS.PowerSummary.iModel")) {
                info.name = info.name.value() + (info.name.empty() ? "" : " ") + *res;
            }
            info.type = BasicInfo::Type::Xml;
            return std::move(info);
        } else {
            return unexpected(props.error());
        }
    } else {
        return unexpected(prod.error());
    }
}

Expected<BasicInfo> Protocols::trySnmp(
    const std::string& ipAddress, uint16_t port, const std::string& community, const std::string& secId) const
{
    return readSnmp(ipAddress, port, community, secId);
}

void Protocols::sortProtocols(std::vector<BasicInfo>& protocols)
{
    static std::set<std::string> epdus = {
        "EATON-EPDU-MIB", "EATON-OIDS", "EATON-GENESIS-II-MIB", "EATON-EPDU-PU-SW-MIB", "ACS-MIB"};

    static std::set<std::string> atss = {"EATON-OIDS", "PowerNet-MIB"};

    auto isMib = [](const BasicInfo& info, const std::set<std::string>& mibs) {
        for (const auto& mib : info.mibs) {
            if (mibs.count(mib)) {
                return true;
            }
        }
        return false;
    };

    std::sort(protocols.begin(), protocols.end(), [&](const BasicInfo& l, const BasicInfo& r) {
        if (l.type == BasicInfo::Type::Snmp && (isMib(l, epdus) || isMib(l, atss))) {
            return false;
        }
        if (r.type == BasicInfo::Type::Snmp && (isMib(r, epdus) || isMib(r, atss))) {
            return true;
        }
        return true;
    });
}


// =====================================================================================================================

} // namespace fty::job
