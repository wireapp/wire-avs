"""Upload a video file to a conversation.

This lazy version has all the parameters hardcoded way down.
"""

import base64, codecs, getpass, hashlib, httplib, json, sys, uuid, urllib

user_agent = "vidtest"

def get_token(host, username, password=None):
    """Get an access token for backend *host* using *username* and *password*.

    If *password* is ``None``, asks for a password. If login fails, simply
    quits.

    Returns an access token.
    """
    if password is None:
        password = getpass.getpass("Password: ")
    conn = httplib.HTTPSConnection(host)
    conn.request("POST", "/login", json.dumps({"email": username,
                                               "password": password}),
                 {"Content-Type": "application/json",
                  "User-Agent": user_agent})
    response = conn.getresponse()
    if response.status != 200:
        print "Login failed."
        sys.exit(1)

    token = json.loads(response.read())["access_token"]
    conn.close()
    return token


def upload(host, token, fp, cid, ctype, **kwargs):
    if isinstance(fp, basestring):
        fp = open(fp, "rb")
    body = fp.read()
    disp = "zasset;conv_id=%s;md5=%s" \
               % (cid, base64.b64encode(hashlib.md5(body).digest()))
    for key, value in kwargs.iteritems():
        if isinstance(value, unicode):
            value = base64.b64encode(codecs.encode(value, "utf-8"))
        disp = "%s;%s=%s" % (disp, key, value)

    conn = httplib.HTTPSConnection(host)
    conn.request("POST", "/assets", body,
                 { "Authorization": "Bearer %s" % token,
                   "Content-Type": ctype,
                   "Content-Disposition": disp,
                   "User-Agent": user_agent })
    response = conn.getresponse()
    if response.status != 201:
        print "%s %s" % (response.status, response.reason)
        print response.read()
        raise RuntimeError()


if __name__ == '__main__':

    host = "staging-nginz-https.zinfra.io"
    username = "foo.bar@wearezeta.com"
    password = None
    conv_id = "29520a50-5b64-44e8-9f1b-b624b8fd0278"
    path = "video.mp4"
    ctype = "video/mp4"

    token = get_token(host, username, password)
    
    upload(host, token, path, conv_id, ctype)

