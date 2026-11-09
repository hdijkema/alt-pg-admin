"""Fetch the PostgreSQL 18 Windows SDK/runtime without running an installer.

Uses HTTPS range requests to avoid downloading the bundled pgAdmin application.
Only the upstream bin/share files needed for isolated tests and client headers/libs
are extracted. No services, PATH changes, or installed databases are touched.
"""
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile
from collections import OrderedDict

URL = 'https://get.enterprisedb.com/postgresql/postgresql-18.6-5-windows-x64-binaries.zip'
ROOT = Path(__file__).resolve().parent.parent / 'third_party' / 'postgresql18'


class RemoteArchive(io.RawIOBase):
    def __init__(self, url):
        self.url = url
        with urllib.request.urlopen(urllib.request.Request(url, method='HEAD'), timeout=60) as response:
            self.size = int(response.headers['Content-Length'])
        self.position = 0
        self.blocks = OrderedDict()

    def seekable(self):
        return True

    def readable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=io.SEEK_SET):
        if whence == io.SEEK_CUR:
            offset += self.position
        elif whence == io.SEEK_END:
            offset += self.size
        if offset < 0:
            raise ValueError('Negative archive offset')
        self.position = offset
        return offset

    def read(self, size=-1):
        if size < 0:
            size = self.size - self.position
        size = min(size, self.size - self.position)
        if size <= 0:
            return b''
        result = bytearray()
        block_size = 1024 * 1024
        while size:
            block = self.position // block_size
            if block not in self.blocks:
                start = block * block_size
                end = min(start + block_size, self.size) - 1
                request = urllib.request.Request(self.url, headers={'Range': f'bytes={start}-{end}'})
                with urllib.request.urlopen(request, timeout=90) as response:
                    if response.status != 206:
                        raise RuntimeError('Server does not support byte-range downloads')
                    data = response.read()
                if len(data) != end - start + 1:
                    raise RuntimeError('Incomplete archive download')
                self.blocks[block] = data
                if len(self.blocks) > 16:
                    self.blocks.popitem(last=False)
            self.blocks.move_to_end(block)
            offset = self.position % block_size
            chunk = self.blocks[block][offset:offset + size]
            result.extend(chunk)
            self.position += len(chunk)
            size -= len(chunk)
        return bytes(result)


def selected(name):
    # Include a disposable server/tooling fixture as well as the application DLLs.
    if name.startswith(('pgsql/bin/', 'pgsql/share/')):
        return True
    if name.startswith('pgsql/include/') and '/' not in name[len('pgsql/include/'):]:
        return True
    return (name.startswith('pgsql/lib/') and name.endswith('.dll')) or name in (
        'pgsql/lib/libpq.lib', 'pgsql/server_license.txt', 'pgsql/commandlinetools_3rd_party_licenses.txt')


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    manifest = {'source': URL, 'version': '18.6-5', 'files': {}}
    with zipfile.ZipFile(RemoteArchive(URL)) as archive:
        entries = [entry for entry in archive.infolist() if not entry.is_dir() and selected(entry.filename)]
        print(f'Downloading {len(entries)} PostgreSQL files to {ROOT}', flush=True)
        for index, entry in enumerate(entries, 1):
            destination = (ROOT / entry.filename).resolve()
            if ROOT.resolve() not in destination.parents:
                raise RuntimeError('Archive entry escapes dependency directory')
            if destination.exists() and destination.stat().st_size == entry.file_size:
                import zlib
                data = destination.read_bytes()
                if zlib.crc32(data) != entry.CRC:
                    data = archive.read(entry)
            else:
                data = archive.read(entry)  # zipfile also verifies the upstream entry CRC.
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            manifest['files'][entry.filename] = hashlib.sha256(data).hexdigest()
            if index % 25 == 0 or index == len(entries):
                print(f'{index}/{len(entries)} files', flush=True)
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('PostgreSQL 18 libraries and isolated test tools ready.', flush=True)


if __name__ == '__main__':
    main()
