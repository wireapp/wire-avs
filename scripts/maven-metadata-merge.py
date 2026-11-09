#!/usr/bin/env python3
#
# Add a version to an artifact-level maven-metadata.xml.
#
# usage: maven-metadata-merge.py <existing.xml|-> <groupId> <artifactId> <version>
#
# Reads the existing metadata (or starts empty when given '-' or a missing
# file), adds <version>, sets <latest>/<release> to the highest version and
# writes the result to stdout.
#

import os
import re
import sys
import time
import xml.etree.ElementTree as ET


def version_key(v):
    return [(0, int(p), '') if p.isdigit() else (1, 0, p)
            for p in re.split(r'[.\-]', v)]


def main():
    if len(sys.argv) != 5:
        print('usage: {} <existing.xml|-> <groupId> <artifactId> <version>'
              .format(sys.argv[0]), file=sys.stderr)
        return 1

    path, group_id, artifact_id, version = sys.argv[1:]

    versions = set()
    if path != '-' and os.path.exists(path):
        root = ET.parse(path).getroot()
        for v in root.iter('version'):
            if v.text and v.text.strip():
                versions.add(v.text.strip())
    versions.add(version)

    ordered = sorted(versions, key=version_key)

    root = ET.Element('metadata')
    ET.SubElement(root, 'groupId').text = group_id
    ET.SubElement(root, 'artifactId').text = artifact_id
    versioning = ET.SubElement(root, 'versioning')
    ET.SubElement(versioning, 'latest').text = ordered[-1]
    ET.SubElement(versioning, 'release').text = ordered[-1]
    vs = ET.SubElement(versioning, 'versions')
    for v in ordered:
        ET.SubElement(vs, 'version').text = v
    ET.SubElement(versioning, 'lastUpdated').text = \
        time.strftime('%Y%m%d%H%M%S', time.gmtime())

    if hasattr(ET, 'indent'):  # Python 3.9+
        ET.indent(root, space='  ')
    sys.stdout.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    sys.stdout.write(ET.tostring(root, encoding='unicode'))
    sys.stdout.write('\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
