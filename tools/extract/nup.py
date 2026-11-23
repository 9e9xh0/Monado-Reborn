"""Read a .nup package: a game's titles (the base game, an update) with exefs/ and romfs/ already decrypted (T280).

The same reading as the runtime's runtime/services/game_files.c: no keys, nothing converted, the files read in place.
The newest title is the one played. Only packages whose files are stored as they are (not compressed) are accepted.

Layout (little-endian): b"NUP\\x01" at 0, the declared size at 0x40; a TOC at 0x80 (b"TOC\\x01", u32 titles, u64 dirs,
u64 files, u64 names offset from 0x80, u64 names size); titles at 0xb0, 0x40 bytes each (u64 id, u32 root, u32 exefs dir,
u32 romfs dir, u32 version); then the dirs (24 bytes: u32 name offset, u32 name length, u32 first child, u32 children,
u64), the files (80 bytes: u32 name offset, u32 name length, u64 size, u64 data offset, u64 stored size, 32-byte hash,
u8 codec: 0 stored as is), the child list (u32 nodes: below the dir count a dir, else a file) and the names.
"""
import os
import struct

MAGIC = b'NUP\x01'
TITLES_AT = 0xb0
MAX_TABLES = 256 << 20


class NupError(Exception):
    """A package that can't be used, with the reason in words."""


def is_package(path):
    """Whether path is a file that starts like a .nup package (by its magic, never by its name)."""
    try:
        with open(path, 'rb') as f:
            return f.read(4) == MAGIC
    except OSError:
        return False


class NupPackage:
    def __init__(self, path):
        self.path = os.fspath(path)
        size = os.path.getsize(self.path)
        with open(self.path, 'rb') as f:
            head = f.read(0xb0)
            if len(head) < 0xb0 or head[:4] != MAGIC or head[0x80:0x84] != b'TOC\x01':
                raise NupError(f'{self.path} isn\'t a .nup package')
            declared = struct.unpack_from('<Q', head, 0x40)[0]
            if declared and size < declared:
                raise NupError(f'{self.path} is incomplete (smaller than its header says): copy or make it again')
            names_off, names_size = struct.unpack_from('<QQ', head, 0x98)
            tables_size = 0x80 + names_off + names_size
            if tables_size > MAX_TABLES or tables_size > size:
                raise NupError(f'{self.path}: its table of contents can\'t be read')
            f.seek(0)
            tables = f.read(tables_size)
        self._parse(tables, size)

    def _parse(self, t, size):
        u32 = lambda o: struct.unpack_from('<I', t, o)[0]  # noqa: E731
        u64 = lambda o: struct.unpack_from('<Q', t, o)[0]  # noqa: E731
        ntitles, ndirs, nfiles = u32(0x84), u64(0x88), u64(0x90)
        names = 0x80 + u64(0x98)
        if not 0 < ntitles <= 64 or not 0 < ndirs <= 1 << 24 or nfiles > 1 << 24:
            raise NupError(f'{self.path}: its table of contents can\'t be read')
        dirs_at = TITLES_AT + 0x40 * ntitles
        files_at = dirs_at + 24 * ndirs
        kids_at = files_at + 80 * nfiles
        if kids_at > names:
            raise NupError(f'{self.path}: its table of contents can\'t be read')
        self.ndirs = ndirs
        self.names, self.first, self.count, self.sizes, self.offsets = [], [], [], [], []
        for i in range(ndirs + nfiles):
            is_dir = i < ndirs
            at = dirs_at + 24 * i if is_dir else files_at + 80 * (i - ndirs)
            name_at, name_len = names + u32(at), u32(at + 4)
            self.names.append(t[name_at:name_at + name_len].decode('utf-8', 'replace'))
            if is_dir:
                self.first.append(u32(at + 8))
                self.count.append(u32(at + 12))
                self.sizes.append(0)
                self.offsets.append(0)
                continue
            file_size, offset, stored, codec = u64(at + 8), u64(at + 16), u64(at + 24), t[at + 64]
            if codec != 0 or stored != file_size:
                raise NupError(f'{self.path}: {self.names[-1]} is stored compressed. Monado reads .nup packages whose '
                               'files are stored as they are: make the package again without compression')
            if offset > size or file_size > size - offset:
                raise NupError(f'{self.path} is incomplete (a file lies past its end): copy or make it again')
            self.first.append(0)
            self.count.append(0)
            self.sizes.append(file_size)
            self.offsets.append(offset)
        self.children = [u32(kids_at + 4 * k) for k in range((names - kids_at) // 4)]
        best = None
        for i in range(ntitles):
            at = TITLES_AT + 0x40 * i
            exefs, romfs, version = u32(at + 12), u32(at + 16), u32(at + 20)
            if best is None or version > best[2]:
                best = (exefs, romfs, version)
        self.exefs, self.romfs, self.version = best
        if self.exefs >= ndirs:
            raise NupError(f'{self.path}: its newest title has no exefs')
        if self.romfs >= ndirs:
            raise NupError(f'{self.path}: its newest title has no romfs')

    def _kids(self, node):
        return self.children[self.first[node]:self.first[node] + self.count[node]]

    def walk(self, top):
        """(path below top, size, node) of every file under 'exefs' or 'romfs'."""
        start = self.exefs if top == 'exefs' else self.romfs
        stack = [(start, '')]
        while stack:
            node, prefix = stack.pop()
            for child in self._kids(node):
                rel = prefix + self.names[child]
                if child < self.ndirs:
                    stack.append((child, rel + '/'))
                else:
                    yield rel, self.sizes[child], child

    def find(self, rel):
        """The file node of 'exefs/main' or 'romfs/a/b', or None."""
        top, _, rest = rel.partition('/')
        node = {'exefs': self.exefs, 'romfs': self.romfs}.get(top)
        for part in rest.split('/') if node is not None else []:
            hits = [c for c in self._kids(node) if self.names[c] == part] if node < self.ndirs else []
            if not hits:
                return None
            node = hits[0]
        return node if node is not None and node >= self.ndirs else None

    def read(self, rel, size=None):
        """A file's bytes (its first size bytes when given)."""
        node = self.find(rel)
        if node is None:
            raise NupError(f'{self.path}: no {rel} in its newest title')
        want = self.sizes[node] if size is None else min(size, self.sizes[node])
        with open(self.path, 'rb') as f:
            f.seek(self.offsets[node])
            return f.read(want)
