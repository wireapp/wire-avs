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
//#include "fakes.hpp"


const char fake_certificate_rsa[] =

"-----BEGIN CERTIFICATE-----\r\n"
"MIIDtzCCAp+gAwIBAgIJAMDaExhGKnoQMA0GCSqGSIb3DQEBCwUAMHExCzAJBgNV\r\n"
"BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMQ8wDQYDVQQHDAZCZXJsaW4xDTAL\r\n"
"BgNVBAoMBFpldGExDjAMBgNVBAMMBXp0ZXN0MR0wGwYJKoZIhvcNAQkBFg56dGVz\r\n"
"dEB6ZXRhLmNvbTAgFw0xNTA4MTUxNDA1MjNaGA8yMTE1MDcyMjE0MDUyM1owcTEL\r\n"
"MAkGA1UEBhMCQVUxEzARBgNVBAgMClNvbWUtU3RhdGUxDzANBgNVBAcMBkJlcmxp\r\n"
"bjENMAsGA1UECgwEWmV0YTEOMAwGA1UEAwwFenRlc3QxHTAbBgkqhkiG9w0BCQEW\r\n"
"Dnp0ZXN0QHpldGEuY29tMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA\r\n"
"wvJM/DyZzRMFbjCg2kvfVxR08dZ1CLHRgjAC41PdzerbW8OL06KGTAjL6jHoIYmp\r\n"
"dHoe8y6gOeIScflMSjxDqjl+WGi++wcbjKZNzG3D9ibpAoKGgCpvzlhA/xiv3p1+\r\n"
"oflZ7M1JtAJriknx3NicrgcpxgpK8AR19SdQzgBIbXh529bqJ4002wJt5lmgBzIt\r\n"
"aWmJr6xIBz+n1QtYuqE5Ozibk6+/ScZbPk0VBQGnMiLZgWcB+ScW7nutOPJUQ+/7\r\n"
"u3zMToHD4/jkc1KnQza+0BnRoYHzhqjL+bn4kP9c8+RZYu7fs/fA0KJqx9pGu+t2\r\n"
"L/7y1E7MctmdQwe80NpGpQIDAQABo1AwTjAdBgNVHQ4EFgQULKuuBKDcg69xOAde\r\n"
"nxF1QUvRH+swHwYDVR0jBBgwFoAULKuuBKDcg69xOAdenxF1QUvRH+swDAYDVR0T\r\n"
"BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEApyDaRaBD2m16Qw06HLACck6b2Sdc\r\n"
"vDA4GyQJz4EzgkJg0FDf49syJLUDFj5uDAn/6oKQ/Qh+QOzUS+pRzRG9+9asG6k0\r\n"
"KZ/LUXvy9cWNzoUl0FTl2+9SQpQr2dCsEkTOg8PUoKqaqqCGxHIT6GPuy4RQxMx3\r\n"
"ao9LDS1IfSbMHL6vSqjbqypECpySBPbJafNuVnrXKuvihe4KNdMF4PdCjLSCdpE2\r\n"
"VSuurLflT/CLxDLmbB5gKsVBGhF04gAYPM1NHuZcbAgr0vZZ7S01S5cXpOn7gyCD\r\n"
"PhEQVA7yLoN0rtvAwO0WN5HClkVn4Z+gC7OavrvWJQ3XGPZUbtBd31my0Q==\r\n"
"-----END CERTIFICATE-----\r\n"

"-----BEGIN PRIVATE KEY-----\r\n"
"MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDC8kz8PJnNEwVu\r\n"
"MKDaS99XFHTx1nUIsdGCMALjU93N6ttbw4vTooZMCMvqMeghial0eh7zLqA54hJx\r\n"
"+UxKPEOqOX5YaL77BxuMpk3MbcP2JukCgoaAKm/OWED/GK/enX6h+VnszUm0AmuK\r\n"
"SfHc2JyuBynGCkrwBHX1J1DOAEhteHnb1uonjTTbAm3mWaAHMi1paYmvrEgHP6fV\r\n"
"C1i6oTk7OJuTr79Jxls+TRUFAacyItmBZwH5Jxbue6048lRD7/u7fMxOgcPj+ORz\r\n"
"UqdDNr7QGdGhgfOGqMv5ufiQ/1zz5Fli7t+z98DQomrH2ka763Yv/vLUTsxy2Z1D\r\n"
"B7zQ2kalAgMBAAECggEASs4ndHLcobC22L2YO6v5m7Sh21rrtdZmCorZ0NmRtz5z\r\n"
"xhaXRTKMTgjiFo/n/1aPy75AuhgeK5fwdAOxnprZbCx7qvkmr7TegW4fVF6aX442\r\n"
"w1vKuMuP86Ix7rFOayIMQaMpOpDbw1NaaMpPIOuN08mz4eZ5MvjDOrtHaoT5RefV\r\n"
"hm7AWgkzRnEu01H1D8LP5yqHJMy0ytql1NyMn1Ix14eSy4HA053HtTaHvLHXhq/S\r\n"
"dpz2xcgMndhtKkOuqhiaOE8SZbayQ2e7ShMra5kn8oBNDYU09J8hrxlsVHzC/yFx\r\n"
"D+l1323/0sBvWLufP1cG7IV073eAlpC83QpnPClYgQKBgQDhellUqUHWKRjOaOfz\r\n"
"6POiAaoT8DxNCtQSRjVXijj6sCne/BIdS3RMcVQO9KAQPSqDbGkqZHGG4xbczcWh\r\n"
"lEMtcIidjovbF5E/RbF9SKrQ9VX9PERMXgDf1diWY++9Etk3TS//xPpW0nehxQ0x\r\n"
"I3eGa/8G7SvUyDAUyjgYeLEUPQKBgQDdVexu7Krshc8u+nvjj1J1pCZqJlXx3pQ2\r\n"
"+QwlTuCEJ7BeAvH/wVZ2W/R0GC7kcI8Gfg1h9OY89WUb9zLL34b5hutw82sO2Wok\r\n"
"coA/cztPa65gFWiggMVWuDqF6GXU79xYWXnrXdQCrmVmLJbY0A1C1jznVTx0+n+j\r\n"
"nQSId21aiQKBgQCBj6YpCaD1CGRiptExzFfCbaZnEpHzyxcU8RbRmHEpS3Sj1sAp\r\n"
"6SOIkU410cbvzdXR8sdzPoglc/O9KNg5AlKfl5xIvJIMcLxbWRal4M2WiILCopC0\r\n"
"OQfTlrN/pykowd3i8w1zsKIQpZtsbygnZjPWH9RJDJs1B1rpd1FIboGCGQKBgQCI\r\n"
"048/22qmoOm9fveLa5RsSTe+M0i6JwC1IyyQ+7vrtqVe2K9Fjf2nWZ07D6AddD/W\r\n"
"oaIgRkb2tDT3Hs/2HI7SPsfZoYEzQtBNC8OgddnadRTtLQ7q+fAEdgsnsM0S39z1\r\n"
"eQrXp79ikPD6QuJV0fgAs3QfBiBDqH+zY2PkAQBHEQKBgBM6kgZn9d0d2Tl40xZF\r\n"
"m4mtv2nHfAciyVEwZYczfyA1beJ7aFTsmtuFKCbV4iCxX0PQThZHGUZVoSg+AbxO\r\n"
"qkfFHZ80ifxdw3+9DJk+bVW4RJnUpZQARoYmMmhlnbnjtOnOF2qVuCQnQh/uTpyv\r\n"
"GiqCyl4CpQeONDyasjDg3JDa\r\n"
"-----END PRIVATE KEY-----\r\n"
"";
