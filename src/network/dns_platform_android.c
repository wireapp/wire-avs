/*
* Wire
* Copyright (C) 2018 Wire Swiss GmbH
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

#include "re.h"
#include "avs.h"
#include "dns_platform.h"
#include "avs_network.h"

#include <pthread.h>
#include <sys/time.h>

#include <jni.h>

struct {
	JavaVM *vm;
	jclass iacls;
	jmethodID namemid;
	jmethodID addrmid;
} java;


struct jni_env {
    bool attached;
    JNIEnv *env;
};


static int jni_attach(struct jni_env *je)
{
	int res;
	int err = 0;
	
	je->attached = false;

	if (!java.vm)
		return ENOSYS;

	res = (*java.vm)->GetEnv(java.vm, (void **)&je->env, JNI_VERSION_1_6);
    
	if (res != JNI_OK || je->env == NULL) {
#ifdef ANDROID
		res = (*java.vm)->AttachCurrentThread(java.vm, &je->env, NULL);
#else
		res = (*java.vm)->AttachCurrentThread(java.vm,
						      (void **)&je->env, NULL);
#endif
        
		if (res != JNI_OK) {
			error("dns_android: AttachCurrentThread failed \n");
			err = ENOSYS;
			goto out;
		}
		je->attached = true;
	}
	else {
		debug("dns_android: jenv = %p\n", je->env);
	}

 out:
	return err;
}

static void jni_detach(struct jni_env *je)
{
	if (je->attached) {
		(*java.vm)->DetachCurrentThread(java.vm);
	}
}

int dns_platform_lookup(struct dns_lookup_entry *lent, struct sa *srv)
{
	struct jni_env je;
	jobject jia;
	jstring jhost;
	jobject jaddr;
	jbyteArray addr;
	int err = 0;

	err = jni_attach(&je);
	if (err) {
		warning("dns_android: attach failed: %m\n", err);
		err = ENOENT;
		goto out;
	}

	/* ia = InetAddress.getByName(host) */

	info("dns_android: looking up: %s\n", lent->host);
	jhost = (*je.env)->NewStringUTF(je.env, lent->host);
	jia = (*je.env)->CallStaticObjectMethod(je.env, java.iacls,
						java.namemid,
						jhost);
	/* addr = ia.getAddress() */
	jaddr = (*je.env)->CallObjectMethod(je.env, jia, java.addrmid);
	debug("dns_android: got address: %p\n", jaddr);

	/* Convert byte[] addr to uint8_t* */
	addr = (*je.env)->GetByteArrayElements(je.env, (jbyteArray)jaddr, NULL);

	/* Finally into sa */
	sa_set_in(srv, ntohl(*(uint32_t *)addr), 3478);

 out:
	jni_detach(&je);

	return err;
}


int dns_platform_init(void *arg)
{
        struct jni_env je;
	int err = 0;
	
	java.vm = arg;

	err = jni_attach(&je);
	if (err)
		return ENOSYS;

	java.iacls = (*je.env)->FindClass(je.env, "java/net/InetAddress");
	if (java.iacls == NULL) {
		warning("dns_android: could not FindClass InetAddress\n");
		err = ENOSYS;
		goto out;
	}
	java.iacls = (*je.env)->NewGlobalRef(je.env, java.iacls);

	java.namemid = (*je.env)->GetStaticMethodID(je.env, java.iacls,
				  "getByName",
			          "(Ljava/lang/String;)Ljava/net/InetAddress;");
	if (java.namemid == NULL) {
		warning("dns_android: could not get getByName method id\n");
		err = ENOENT;
		goto out;
	}

	java.addrmid = (*je.env)->GetMethodID(je.env, java.iacls,
					      "getAddress", "()[B");
	if (java.addrmid == NULL) {
		warning("dns_android: failed to getAddres method\n");
		err = ENOENT;
		goto out;
	}
			     
 out:	
	return err;
}


void dns_platform_close(void)
{
}
