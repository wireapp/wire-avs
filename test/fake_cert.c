/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <re.h>
#include <avs.h>


/*
 * Self-signed ECDSA certificate and private key, generated with:
 *
 *   openssl ecparam -genkey -name prime256v1 -out key.pem
 *   openssl req -new -sha256 -key key.pem -out csr.csr
 *   openssl req -x509 -sha256 -days 365 -key key.pem \
 *       -in csr.csr -out certificate.pem
 *
 * This is the same cert as test/data/cert_ecdsa.pem
 */
const char fake_certificate_ecdsa[] =

"-----BEGIN CERTIFICATE-----\r\n"
"MIIBxzCCAW2gAwIBAgIJAPOqWRID8OrdMAoGCCqGSM49BAMCMEAxCzAJBgNVBAYT\r\n"
"AkRFMQ8wDQYDVQQIDAZCZXJsaW4xDTALBgNVBAoMBFdpcmUxETAPBgNVBAMMCHdp\r\n"
"cmUuY29tMB4XDTE4MDMwMTE1MDM1NFoXDTE5MDMwMTE1MDM1NFowQDELMAkGA1UE\r\n"
"BhMCREUxDzANBgNVBAgMBkJlcmxpbjENMAsGA1UECgwEV2lyZTERMA8GA1UEAwwI\r\n"
"d2lyZS5jb20wWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAATMbAK9QsQNUx09z3Ch\r\n"
"sOhBMjjb4bkVk62HwNrtQZnBla5aL+hSCoDPecbvtMniaDQC7yWaroYW5AnfjqI4\r\n"
"2wcjo1AwTjAdBgNVHQ4EFgQUBa6zBqppZ18mO7Bpp1R1a/FOzcowHwYDVR0jBBgw\r\n"
"FoAUBa6zBqppZ18mO7Bpp1R1a/FOzcowDAYDVR0TBAUwAwEB/zAKBggqhkjOPQQD\r\n"
"AgNIADBFAiAfLM3jqO4ZLBPi8GA0DdYoiSAyfoBGI4ZCvNDtj0r+TAIhAMSoi2B+\r\n"
"K0mPnqPhU72iCi528JVamzFLhxjF9zbZJuFp\r\n"
"-----END CERTIFICATE-----\r\n"

"-----BEGIN EC PARAMETERS-----\r\n"
"BggqhkjOPQMBBw==\r\n"
"-----END EC PARAMETERS-----\r\n"
"-----BEGIN EC PRIVATE KEY-----\r\n"
"MHcCAQEEIKv1qGn7jXDKNvMHEjcSV1pNOQ0k75vsktYtvsIR5HpioAoGCCqGSM49\r\n"
"AwEHoUQDQgAEzGwCvULEDVMdPc9wobDoQTI42+G5FZOth8Da7UGZwZWuWi/oUgqA\r\n"
"z3nG77TJ4mg0Au8lmq6GFuQJ346iONsHIw==\r\n"
"-----END EC PRIVATE KEY-----\r\n"

	"";
