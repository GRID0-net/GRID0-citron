// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <sstream>
#include <string>

#include "common/alignment.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/sockets/private_server.h"
#include "core/hle/service/ssl/cert_store.h"
#include "core/hle/service/ssl/switchnet_ca.h"

namespace Service::SSL {

namespace {
// Ids for certificates that are not in the console's store. Outside Nintendo's numbering.
constexpr s32 PrivateServerCertificateIdBase = 0x7E000000;

/// DER bodies of every CERTIFICATE block in a PEM text.
std::vector<std::vector<u8>> PemToDer(std::string_view pem) {
    static constexpr std::string_view Begin = "-----BEGIN CERTIFICATE-----";
    static constexpr std::string_view End = "-----END CERTIFICATE-----";
    std::vector<std::vector<u8>> out;
    size_t pos = 0;
    while ((pos = pem.find(Begin, pos)) != std::string_view::npos) {
        const size_t body = pos + Begin.size();
        const size_t end = pem.find(End, body);
        if (end == std::string_view::npos) {
            break;
        }
        std::vector<u8> der;
        u32 buffer = 0;
        int bits = 0;
        for (const char c : pem.substr(body, end - body)) {
            int value;
            if (c >= 'A' && c <= 'Z') {
                value = c - 'A';
            } else if (c >= 'a' && c <= 'z') {
                value = c - 'a' + 26;
            } else if (c >= '0' && c <= '9') {
                value = c - '0' + 52;
            } else if (c == '+') {
                value = 62;
            } else if (c == '/') {
                value = 63;
            } else {
                continue; // whitespace and '=' padding
            }
            buffer = (buffer << 6) | static_cast<u32>(value);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                der.push_back(static_cast<u8>((buffer >> bits) & 0xFF));
            }
        }
        if (!der.empty()) {
            out.push_back(std::move(der));
        }
        pos = end + End.size();
    }
    return out;
}

std::string DescribeIds(std::span<const CaCertificateId> ids) {
    std::string out;
    for (const auto id : ids) {
        out += fmt::format("{}{}", out.empty() ? "" : ",", static_cast<s32>(id));
    }
    return out.empty() ? "none" : out;
}
} // namespace

// https://switchbrew.org/wiki/SSL_services#CertStore

CertStore::CertStore(Core::System& system) {
    constexpr u64 CertStoreDataId = 0x0100000000000800ULL;

    auto& fsc = system.GetFileSystemController();

    // Attempt to load certificate data from storage
    const auto nca =
        fsc.GetSystemNANDContents()->GetEntry(CertStoreDataId, FileSys::ContentRecordType::Data);
    if (!nca) {
        LOG_WARNING(Service_SSL,
                    "No system CertStore (system data {:016X} is not installed): games that load "
                    "trusted root certificates (Splatoon 3's gRPC client) get none. Install "
                    "firmware to fix.",
                    CertStoreDataId);
        return;
    }
    const auto romfs = nca->GetRomFS();
    if (!romfs) {
        LOG_WARNING(Service_SSL, "The CertStore system data could not be read (missing keys?)");
        return;
    }
    const auto extracted = FileSys::ExtractRomFS(romfs);
    if (!extracted) {
        LOG_ERROR(Service_SSL, "CertStore could not be extracted, corrupt RomFS?");
        return;
    }
    const auto cert_store_file = extracted->GetFile("ssl_TrustedCerts.bdf");
    if (!cert_store_file) {
        LOG_ERROR(Service_SSL, "Failed to find trusted certificates in CertStore");
        return;
    }

    // Read and verify the header.
    CertStoreHeader header;
    cert_store_file->ReadObject(std::addressof(header));

    if (header.magic != Common::MakeMagic('s', 's', 'l', 'T')) {
        LOG_ERROR(Service_SSL, "Invalid certificate store magic");
        return;
    }

    // Ensure the file can contains the number of entries it says it does.
    const u64 expected_size = sizeof(header) + sizeof(CertStoreEntry) * header.num_entries;
    const u64 actual_size = cert_store_file->GetSize();
    if (actual_size < expected_size) {
        LOG_ERROR(Service_SSL, "Size mismatch, expected at least {} bytes, got {}", expected_size,
                  actual_size);
        return;
    }

    // Read entries.
    std::vector<CertStoreEntry> entries(header.num_entries);
    cert_store_file->ReadArray(entries.data(), header.num_entries, sizeof(header));

    // Insert into memory store.
    for (const auto& entry : entries) {
        m_certs.emplace(entry.certificate_id,
                        Certificate{
                            .status = entry.certificate_status,
                            .der_data = cert_store_file->ReadBytes(
                                entry.der_size, entry.der_offset + sizeof(header)),
                        });
    }
    LOG_INFO(Service_SSL, "Loaded {} trusted certificates from the system CertStore",
             m_certs.size());
}

std::map<CaCertificateId, CertStore::Certificate> CertStore::PrivateServerCertificates() const {
    std::map<CaCertificateId, Certificate> out;
    if (Service::Sockets::PrivateServerAddress().empty()) {
        return out;
    }
    // A console set up for SwitchNet trusts its CA system-wide, so a game that builds its own
    // TLS trust store from this list (gRPC does) trusts the private server too. Without it the
    // game's NPLN client cannot verify the GRID0+ server.
    std::string pem{SwitchNetLocalCaPem};
    const std::string& bundle = Settings::values.private_server_ca_bundle.GetValue();
    if (!bundle.empty()) {
        std::ifstream file{bundle};
        std::stringstream text;
        text << file.rdbuf();
        if (file) {
            pem = text.str();
        } else {
            LOG_ERROR(Service_SSL, "Could not read the private server CA bundle {}", bundle);
        }
    }
    s32 id = PrivateServerCertificateIdBase;
    for (auto& der : PemToDer(pem)) {
        out.emplace(static_cast<CaCertificateId>(id++),
                    Certificate{.status = TrustedCertStatus::EnabledTrusted,
                                .der_data = std::move(der)});
    }
    return out;
}

CertStore::~CertStore() = default;

template <typename F>
void CertStore::ForEachCertificate(std::span<const CaCertificateId> certificate_ids, F&& f) {
    if (certificate_ids.size() == 1 && certificate_ids.front() == CaCertificateId::All) {
        for (const auto& entry : m_certs) {
            f(entry);
        }
    } else {
        for (const auto certificate_id : certificate_ids) {
            const auto entry = m_certs.find(certificate_id);
            if (entry == m_certs.end()) {
                continue;
            }
            f(*entry);
        }
    }
    // The private server's CA is appended to every request while a redirect is active.
    for (const auto& entry : PrivateServerCertificates()) {
        f(entry);
    }
}

Result CertStore::GetCertificates(u32* out_num_entries, std::span<u8> out_data,
                                  std::span<const CaCertificateId> certificate_ids) {
    // Ensure the buffer is large enough to hold the output.
    u32 required_size;
    R_TRY(this->GetCertificateBufSize(std::addressof(required_size), out_num_entries,
                                      certificate_ids));
    if (out_data.size_bytes() < required_size) {
        LOG_WARNING(Service_SSL, "GetCertificates: buffer of {} bytes is smaller than the {} required",
                    out_data.size_bytes(), required_size);
    }
    R_UNLESS(out_data.size_bytes() >= required_size, ResultUnknown);

    // Make parallel arrays.
    std::vector<BuiltInCertificateInfo> cert_infos;
    std::vector<u8> der_datas;

    const u32 der_data_offset = (*out_num_entries + 1) * sizeof(BuiltInCertificateInfo);
    u32 cur_der_offset = der_data_offset;

    // Fill output.
    this->ForEachCertificate(certificate_ids, [&](auto& entry) {
        const auto& [status, cur_der_data] = entry.second;
        BuiltInCertificateInfo cert_info{
            .cert_id = entry.first,
            .status = status,
            .der_size = cur_der_data.size(),
            .der_offset = cur_der_offset,
        };

        cert_infos.push_back(cert_info);
        der_datas.insert(der_datas.end(), cur_der_data.begin(), cur_der_data.end());
        cur_der_offset += static_cast<u32>(cur_der_data.size());
    });

    // Append terminator entry.
    cert_infos.push_back(BuiltInCertificateInfo{
        .cert_id = CaCertificateId::All,
        .status = TrustedCertStatus::Invalid,
        .der_size = 0,
        .der_offset = 0,
    });

    // Write to output span.
    std::memcpy(out_data.data(), cert_infos.data(),
                cert_infos.size() * sizeof(BuiltInCertificateInfo));
    std::memcpy(out_data.data() + der_data_offset, der_datas.data(), der_datas.size());

    if (Service::Sockets::NetworkDiagnosticsEnabled() || *out_num_entries == 0) {
        LOG_WARNING(Service_SSL,
                    "GetCertificates(ids={}) -> {} certificate(s), {} bytes ({} from the system "
                    "store, the rest the private server CA)",
                    DescribeIds(certificate_ids), *out_num_entries, required_size, m_certs.size());
    }
    R_SUCCEED();
}

Result CertStore::GetCertificateBufSize(u32* out_size, u32* out_num_entries,
                                        std::span<const CaCertificateId> certificate_ids) {
    // Output size is at least the size of the terminator entry.
    *out_size = sizeof(BuiltInCertificateInfo);
    *out_num_entries = 0;

    this->ForEachCertificate(certificate_ids, [&](auto& entry) {
        *out_size += sizeof(BuiltInCertificateInfo);
        *out_size += Common::AlignUp(static_cast<u32>(entry.second.der_data.size()), 4);
        (*out_num_entries)++;
    });

    R_SUCCEED();
}

} // namespace Service::SSL
