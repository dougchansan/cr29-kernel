#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SECURITY_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <security.h>
#include <schannel.h>
#include <string>
#include <vector>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")

class TlsSocket {
    SOCKET sock = INVALID_SOCKET;
    CredHandle hCred = {0, 0};
    CtxtHandle hContext = {0, 0};
    SecPkgContext_StreamSizes sizes = {};
    std::vector<char> recvBuffer;
    std::vector<char> decryptedData;
    bool tlsEnabled = false;
    bool contextValid = false;
    bool credValid = false;
    bool handshakeComplete = false;

public:
    TlsSocket() {
        recvBuffer.resize(16384);
    }

    ~TlsSocket() {
        close();
    }

    bool connect(const std::string& host, int port, bool useTls) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
            return false;
        }

        struct addrinfo hints = {}, *result;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            std::cerr << "DNS resolution failed for " << host << "\n";
            return false;
        }

        sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (sock == INVALID_SOCKET) {
            freeaddrinfo(result);
            std::cerr << "Socket creation failed\n";
            return false;
        }

        if (::connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            freeaddrinfo(result);
            std::cerr << "TCP connect failed\n";
            return false;
        }

        freeaddrinfo(result);

        if (useTls) {
            tlsEnabled = true;
            return performTlsHandshake(host);
        }

        return true;
    }

    bool performTlsHandshake(const std::string& host) {
        std::cout << "[TLS] Starting handshake with " << host << "\n";
        std::cout.flush();

        // Acquire credentials
        SCHANNEL_CRED cred = {};
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;

        SECURITY_STATUS acqStatus = AcquireCredentialsHandleA(NULL, (LPSTR)UNISP_NAME_A, SECPKG_CRED_OUTBOUND,
                                       NULL, &cred, NULL, NULL, &hCred, NULL);
        if (acqStatus != SEC_E_OK) {
            std::cerr << "[TLS] AcquireCredentialsHandle failed: 0x" << std::hex << acqStatus << std::dec << "\n";
            return false;
        }
        credValid = true;
        std::cout << "[TLS] Credentials acquired\n";
        std::cout.flush();

        std::cout << "[TLS] Setting up buffers...\n";
        std::cout.flush();

        SecBuffer outBuffers[1];
        SecBufferDesc outBufferDesc;
        outBuffers[0].pvBuffer = NULL;
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        outBuffers[0].cbBuffer = 0;
        outBufferDesc.ulVersion = SECBUFFER_VERSION;
        outBufferDesc.cBuffers = 1;
        outBufferDesc.pBuffers = outBuffers;

        DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                      ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
        DWORD outFlags = 0;

        std::cout << "[TLS] Calling InitializeSecurityContextA...\n";
        std::cout.flush();

        SECURITY_STATUS status = InitializeSecurityContextA(
            &hCred, NULL, (SEC_CHAR*)host.c_str(), flags, 0, 0,
            NULL, 0, &hContext, &outBufferDesc, &outFlags, NULL);

        std::cout << "[TLS] Initial context status: 0x" << std::hex << status << std::dec << "\n";
        std::cout.flush();

        if (status != SEC_I_CONTINUE_NEEDED) {
            std::cerr << "[TLS] InitializeSecurityContext failed: 0x" << std::hex << status << std::dec << "\n";
            return false;
        }
        contextValid = true;

        // Send initial handshake
        std::cout << "[TLS] Sending initial handshake (" << outBuffers[0].cbBuffer << " bytes)...\n";
        std::cout.flush();
        if (outBuffers[0].cbBuffer > 0) {
            int sent = send(sock, (char*)outBuffers[0].pvBuffer, outBuffers[0].cbBuffer, 0);
            if (sent <= 0) {
                std::cerr << "[TLS] Failed to send initial handshake\n";
                FreeContextBuffer(outBuffers[0].pvBuffer);
                return false;
            }
            std::cout << "[TLS] Sent " << sent << " bytes\n";
            std::cout.flush();
            FreeContextBuffer(outBuffers[0].pvBuffer);
        }

        // Continue handshake
        std::vector<char> handshakeBuffer(16384);
        int handshakeLen = 0;
        int roundNum = 0;

        std::cout << "[TLS] Entering handshake loop...\n";
        std::cout.flush();

        while (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE) {
            roundNum++;
            std::cout << "[TLS] Handshake round " << roundNum << " status=0x" << std::hex << status << std::dec << "\n";
            std::cout.flush();

            if (status == SEC_E_INCOMPLETE_MESSAGE || handshakeLen == 0) {
                std::cout << "[TLS] Waiting for server response...\n";
                std::cout.flush();
                int received = recv(sock, handshakeBuffer.data() + handshakeLen,
                                    (int)(handshakeBuffer.size() - handshakeLen), 0);
                if (received <= 0) {
                    std::cerr << "[TLS] recv() failed: " << received << "\n";
                    return false;
                }
                handshakeLen += received;
                std::cout << "[TLS] Received " << received << " bytes (total: " << handshakeLen << ")\n";
                std::cout.flush();
            }

            SecBuffer inBuffers[2];
            SecBufferDesc inBufferDesc;
            inBuffers[0].pvBuffer = handshakeBuffer.data();
            inBuffers[0].cbBuffer = handshakeLen;
            inBuffers[0].BufferType = SECBUFFER_TOKEN;
            inBuffers[1].pvBuffer = NULL;
            inBuffers[1].cbBuffer = 0;
            inBuffers[1].BufferType = SECBUFFER_EMPTY;
            inBufferDesc.ulVersion = SECBUFFER_VERSION;
            inBufferDesc.cBuffers = 2;
            inBufferDesc.pBuffers = inBuffers;

            outBuffers[0].pvBuffer = NULL;
            outBuffers[0].BufferType = SECBUFFER_TOKEN;
            outBuffers[0].cbBuffer = 0;
            outBufferDesc.ulVersion = SECBUFFER_VERSION;
            outBufferDesc.cBuffers = 1;
            outBufferDesc.pBuffers = outBuffers;

            std::cout << "[TLS] Calling ISC with " << handshakeLen << " bytes...\n";
            std::cout.flush();

            status = InitializeSecurityContextA(
                &hCred, &hContext, (SEC_CHAR*)host.c_str(), flags, 0, 0,
                &inBufferDesc, 0, &hContext, &outBufferDesc, &outFlags, NULL);

            std::cout << "[TLS] ISC returned: 0x" << std::hex << status << std::dec
                      << " outBytes=" << outBuffers[0].cbBuffer << "\n";
            std::cout.flush();

            if (status == SEC_E_OK) {
                // Handshake complete - send any final data if needed
                if (outBuffers[0].cbBuffer > 0 && outBuffers[0].pvBuffer != NULL) {
                    std::cout << "[TLS] Sending final " << outBuffers[0].cbBuffer << " bytes\n";
                    std::cout.flush();
                    send(sock, (char*)outBuffers[0].pvBuffer, outBuffers[0].cbBuffer, 0);
                    FreeContextBuffer(outBuffers[0].pvBuffer);
                }
                std::cout << "[TLS] Handshake completed successfully!\n";
                std::cout.flush();
                break;  // Exit loop - handshake complete
            }
            else if (status == SEC_I_CONTINUE_NEEDED) {
                if (outBuffers[0].cbBuffer > 0 && outBuffers[0].pvBuffer != NULL) {
                    std::cout << "[TLS] Sending " << outBuffers[0].cbBuffer << " bytes to server\n";
                    std::cout.flush();
                    if (send(sock, (char*)outBuffers[0].pvBuffer, outBuffers[0].cbBuffer, 0) <= 0) {
                        FreeContextBuffer(outBuffers[0].pvBuffer);
                        return false;
                    }
                    FreeContextBuffer(outBuffers[0].pvBuffer);
                }

                // Handle extra data
                if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0) {
                    std::cout << "[TLS] Extra data: " << inBuffers[1].cbBuffer << " bytes\n";
                    std::cout.flush();
                    memmove(handshakeBuffer.data(),
                            handshakeBuffer.data() + handshakeLen - inBuffers[1].cbBuffer,
                            inBuffers[1].cbBuffer);
                    handshakeLen = inBuffers[1].cbBuffer;
                } else {
                    handshakeLen = 0;
                }
            }
        }

        std::cout << "[TLS] Exited handshake loop, status=0x" << std::hex << status << std::dec << "\n";
        std::cout.flush();

        if (status != SEC_E_OK) {
            std::cerr << "[TLS] Handshake failed with status: 0x" << std::hex << status << std::dec << "\n";
            return false;
        }

        std::cout << "[TLS] Getting stream sizes...\n";
        std::cout << "[TLS] hContext = {" << hContext.dwLower << ", " << hContext.dwUpper << "}\n";
        std::cout.flush();

        // Get stream sizes for encryption/decryption
        SecPkgContext_StreamSizes tempSizes = {};
        std::cout << "[TLS] Calling QueryContextAttributes...\n";
        std::cout.flush();
        SECURITY_STATUS qStatus = QueryContextAttributes(&hContext, SECPKG_ATTR_STREAM_SIZES, &tempSizes);
        std::cout << "[TLS] QueryContextAttributes returned: 0x" << std::hex << qStatus << std::dec << "\n";
        std::cout.flush();
        if (qStatus != SEC_E_OK) {
            std::cerr << "[TLS] Failed to get stream sizes: 0x" << std::hex << qStatus << std::dec << "\n";
            return false;
        }
        sizes = tempSizes;

        std::cout << "[TLS] Handshake complete. Header=" << sizes.cbHeader
                  << " Trailer=" << sizes.cbTrailer << " MaxMsg=" << sizes.cbMaximumMessage << "\n";
        std::cout.flush();
        handshakeComplete = true;
        return true;
    }

    int sendData(const char* data, int len) {
        if (!tlsEnabled) {
            return send(sock, data, len, 0);
        }

        if (!handshakeComplete || !contextValid) {
            std::cerr << "[TLS] Cannot send: handshake not complete\n";
            return -1;
        }

        // Encrypt data
        std::vector<char> buffer(sizes.cbHeader + len + sizes.cbTrailer);

        SecBuffer buffers[4];
        buffers[0].pvBuffer = buffer.data();
        buffers[0].cbBuffer = sizes.cbHeader;
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;

        memcpy(buffer.data() + sizes.cbHeader, data, len);
        buffers[1].pvBuffer = buffer.data() + sizes.cbHeader;
        buffers[1].cbBuffer = len;
        buffers[1].BufferType = SECBUFFER_DATA;

        buffers[2].pvBuffer = buffer.data() + sizes.cbHeader + len;
        buffers[2].cbBuffer = sizes.cbTrailer;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;

        buffers[3].pvBuffer = NULL;
        buffers[3].cbBuffer = 0;
        buffers[3].BufferType = SECBUFFER_EMPTY;

        SecBufferDesc bufferDesc;
        bufferDesc.ulVersion = SECBUFFER_VERSION;
        bufferDesc.cBuffers = 4;
        bufferDesc.pBuffers = buffers;

        if (EncryptMessage(&hContext, 0, &bufferDesc, 0) != SEC_E_OK) {
            return -1;
        }

        int totalLen = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
        if (send(sock, buffer.data(), totalLen, 0) != totalLen) {
            return -1;
        }

        return len;
    }

    int recvData(char* data, int maxLen) {
        if (!tlsEnabled) {
            return recv(sock, data, maxLen, 0);
        }

        if (!handshakeComplete || !contextValid) {
            std::cerr << "[TLS] Cannot recv: handshake not complete\n";
            return -1;
        }

        // Return buffered decrypted data first
        if (!decryptedData.empty()) {
            int copyLen = (std::min)((int)decryptedData.size(), maxLen);
            memcpy(data, decryptedData.data(), copyLen);
            decryptedData.erase(decryptedData.begin(), decryptedData.begin() + copyLen);
            return copyLen;
        }

        // Receive and decrypt
        int received = recv(sock, recvBuffer.data(), (int)recvBuffer.size(), 0);
        if (received <= 0) {
            return received;
        }

        SecBuffer buffers[4];
        buffers[0].pvBuffer = recvBuffer.data();
        buffers[0].cbBuffer = received;
        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[1].BufferType = SECBUFFER_EMPTY;
        buffers[2].BufferType = SECBUFFER_EMPTY;
        buffers[3].BufferType = SECBUFFER_EMPTY;

        SecBufferDesc bufferDesc;
        bufferDesc.ulVersion = SECBUFFER_VERSION;
        bufferDesc.cBuffers = 4;
        bufferDesc.pBuffers = buffers;

        SECURITY_STATUS status = DecryptMessage(&hContext, &bufferDesc, 0, NULL);

        if (status == SEC_E_OK) {
            // Find decrypted data
            for (int i = 0; i < 4; i++) {
                if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0) {
                    int copyLen = (std::min)((int)buffers[i].cbBuffer, maxLen);
                    memcpy(data, buffers[i].pvBuffer, copyLen);

                    // Buffer remaining
                    if ((int)buffers[i].cbBuffer > maxLen) {
                        decryptedData.insert(decryptedData.end(),
                                             (char*)buffers[i].pvBuffer + maxLen,
                                             (char*)buffers[i].pvBuffer + buffers[i].cbBuffer);
                    }
                    return copyLen;
                }
            }
        }

        return -1;
    }

    void close() {
        if (contextValid) {
            DeleteSecurityContext(&hContext);
            contextValid = false;
        }
        if (credValid) {
            FreeCredentialsHandle(&hCred);
            credValid = false;
        }
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        WSACleanup();
    }

    bool isValid() const {
        return sock != INVALID_SOCKET;
    }
};

#endif
