"""Playback a video file uploaded to a conversation through Firefox.

This lazy version has all the parameters hardcoded way down.
"""

import base64, codecs, getpass, hashlib, httplib, json, sys, uuid, urllib
import subprocess

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


def get_asset_link(host, token, cid, aid):
    conn = httplib.HTTPSConnection(host)
    conn.request("GET", "/conversations/%s/assets/%s" % (cid, aid), None,
                 { "Authorization": "Bearer %s" % token,
                   "User-Agent": user_agent })
    response = conn.getresponse()
    if response.status != 302:
        print "%s %s" % (response.status, response.reason)
        print response.read()
        raise RuntimeError()
    return response.getheader("Location")


if __name__ == '__main__':

    host = "staging-nginz-https.zinfra.io"
    username = "foo.bar@wearezeta.com"
    password = None
    conv_id = "29520a50-5b64-44e8-9f1b-b624b8fd0278"
    asset_id = "ec7746ca-ec16-48bc-bc61-fa3a496e2f1b"
    htmlpath = "/tmp/video.html"

    token = get_token(host, username, password)
    url = get_asset_link(host, token, conv_id, asset_id)

    fp = open(htmlpath, "w")
    fp.write('<html><body><video src="%s" controls></video></body></html>' \
                % (url))
    fp.close()
    subprocess.call(["firefox", "--new-window", "file://%s" % htmlpath])

