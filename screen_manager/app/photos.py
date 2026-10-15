"""Photos for the screensaver (MistryneT fork): a folder of pictures on the Home Assistant host, shown one after the other.

The folder is /media/screensaver. Home Assistant's own Network storage (Settings, System, Storage) mounts an SMB or NFS
share under /media, so a folder on a NAS works the same as a local one. Pictures are shown in a shuffled round, each for
`seconds`; a round ends when every picture has been shown, and the next one starts in another order.

Photos uses a pseudo camera, PHOTOS. Legacy screens receive BMP/RLE; screens advertising photo_jpeg receive a
bounded baseline JPEG through the same still-picture path. PhotoShow keeps per-screen finished frames and prepares
the next frame without writing original files.

Faster changes. A photo is made small before it goes anywhere: a file above `max_mb` is skipped, a JPEG is read at a
reduced size by the decoder itself (a camera's 12 megapixel file costs a fraction of a full read), and the picture is cut
to MAX_EDGE on its long side and kept as a 85 % JPEG. The next photo is prepared while the current one is on the glass,
so the change itself only sends what is ready."""
import io
import logging
import os
import random
import time
from pathlib import Path

LOG = logging.getLogger(__name__)

PHOTOS = 'image.screensaver_photos'
FOLDER = os.environ.get('SCREENSAVER_DIR', '/media/screensaver')
EXTENSIONS = frozenset(('.jpg', '.jpeg', '.png', '.webp', '.bmp'))
MAX_FILES = 2000      # more than this in the folder are ignored
MAX_DEPTH = 3         # folders inside the folder are read, this deep
MAX_EDGE = 1920       # the long side a photo is cut to, whatever the glass
MAX_PIXELS = 100_000_000  # a picture larger than this is skipped unread (a decompression bomb)
RESCAN_SECONDS = 60
SECONDS = (5, 3600, 30)  # the shortest, the longest and the default time one photo stays
MEGABYTES = (1, 50, 8)    # the smallest, the largest and the default size of a file that is used


class Photos:
    def __init__(self, folder=FOLDER, clock=time.monotonic, rng=None):
        self.folder, self.clock = folder, clock
        self.rng = rng or random.Random()
        self.files = []          # every usable file, as found
        self.order = []          # the round in progress
        self.shown = ''          # the file on the glass
        self.since = None        # when it went up
        self.scanned = None
        self.prepared = {}       # (path, mtime, size) -> JPEG bytes, the current and the next
        self.skipped = set()     # files that could not be read: not tried again until the folder changes

    # ----- the folder -----
    def scan(self, force=False):
        now = self.clock()
        if not force and self.scanned is not None and now - self.scanned < RESCAN_SECONDS:
            return
        self.scanned = now
        found = []
        base = os.path.abspath(self.folder)
        for root, folders, names in os.walk(base):
            depth = root[len(base):].count(os.sep)
            folders[:] = [] if depth >= MAX_DEPTH else sorted(name for name in folders if not name.startswith('.'))
            for name in sorted(names):
                if name.startswith('.') or os.path.splitext(name)[1].lower() not in EXTENSIONS:
                    continue
                found.append(os.path.join(root, name))
                if len(found) >= MAX_FILES:
                    break
            if len(found) >= MAX_FILES:
                break
        if set(found) != set(self.files):
            self.skipped &= set(found)
        self.files = found
        self.order = [path for path in self.order if path in set(found)]

    def available(self):
        self.scan()
        return bool(self.files)

    def state(self):
        """The pseudo camera's state as Home Assistant words it: `idle` with pictures, `unavailable` without."""
        return {'state': 'idle' if self.available() else 'unavailable',
                'attributes': {'friendly_name': 'Photos', 'entity_picture': ''}}

    # ----- the round -----
    def _usable(self, path, max_bytes):
        try:
            return path not in self.skipped and os.path.getsize(path) <= max_bytes
        except OSError:
            return False

    def _next(self, max_bytes, after=''):
        """The next usable file of the round (a new round, shuffled, when it is spent), never `after` twice in a row
        unless it is the only one."""
        for _ in range(2):
            if not self.order:
                self.order = list(self.files)
                self.rng.shuffle(self.order)
                if len(self.order) > 1 and self.order[0] == after:
                    self.order.append(self.order.pop(0))
            while self.order:
                path = self.order.pop(0)
                if self._usable(path, max_bytes):
                    return path
        return ''

    def current(self, seconds=SECONDS[2], max_mb=MEGABYTES[2]):
        """The file on the glass now: the one before until `seconds` have passed since it went up."""
        self.scan()
        max_bytes = int(max_mb * 1024 * 1024)
        now = self.clock()
        if self.shown and self.since is not None and now - self.since < seconds and self._usable(self.shown, max_bytes) \
                and os.path.exists(self.shown):
            return self.shown
        path = self._next(max_bytes, self.shown)
        if path:
            self.shown, self.since = path, now
        return path

    def upcoming(self, max_mb=MEGABYTES[2]):
        """The file that comes after the current one, for preparing it ahead ('' when it is not known yet)."""
        max_bytes = int(max_mb * 1024 * 1024)
        return next((path for path in self.order if self._usable(path, max_bytes)), '')

    # ----- the picture -----
    def prepare(self, path):
        """The file as a small JPEG (bytes): a long side of at most MAX_EDGE, upright by its EXIF, or None if it cannot be
        read. Run in a worker thread, it is slow."""
        from PIL import Image, ImageOps
        try:
            stat = os.stat(path)
            key = (path, stat.st_mtime_ns, stat.st_size)
            if key in self.prepared:
                return self.prepared[key]
            with Image.open(path) as image:
                if image.width * image.height > MAX_PIXELS:
                    raise ValueError('too many pixels')
                if image.format == 'JPEG':
                    image.draft('RGB', (MAX_EDGE, MAX_EDGE))  # the decoder reads it at 1/2, 1/4 or 1/8 where it can
                image = ImageOps.exif_transpose(image)
                image.thumbnail((MAX_EDGE, MAX_EDGE), Image.LANCZOS)
                if image.mode != 'RGB':
                    image = image.convert('RGB')
                out = io.BytesIO()
                image.save(out, 'JPEG', quality=85)
            data = out.getvalue()
        except Exception as error:
            LOG.info('A screensaver photo cannot be read (%s)', type(error).__name__)
            self.skipped.add(path)
            return None
        if len(self.prepared) >= 3:
            self.prepared.pop(next(iter(self.prepared)))
        self.prepared[key] = data
        return data

    async def image(self, settings=None):
        """The photo for the glass now, as JPEG bytes; the next one is prepared right after. Raises when there is none."""
        import asyncio
        seconds, max_mb = (settings or {}).get('seconds', SECONDS[2]), (settings or {}).get('mb', MEGABYTES[2])
        loop = asyncio.get_running_loop()
        for _ in range(5):
            path = self.current(seconds, max_mb)
            if not path:
                raise FileNotFoundError('no photos')
            data = await loop.run_in_executor(None, self.prepare, path)
            if data is not None:
                ahead = self.upcoming(max_mb)
                if ahead:
                    loop.run_in_executor(None, self.prepare, ahead)
                return data
            self.since = None  # unreadable: take the next one at once
        raise ValueError('no readable photo')


MAX_JPEG_BYTES = 1024 * 1024


def baseline_jpeg(data, logical_box=None):
    """Match still_jpeg::inspect before retaining source bytes for the P4 decoder."""
    size = len(data)
    if size < 4 or size > MAX_JPEG_BYTES or data[:2] != b'\xff\xd8':
        return False
    at, frame, scan, crop = 2, False, False, None
    width = height = 0
    while at < size:
        if data[at] != 255:
            return False
        at += 1
        while at < size and data[at] == 255:
            at += 1
        if at >= size:
            return False
        marker = data[at]
        at += 1
        if marker == 217:
            logical = crop or (width, height)
            return bool(frame and scan and at == size and (not crop or (0 < crop[0] <= width and 0 < crop[1] <= height and ((crop[0] + 7) // 8 * 8, (crop[1] + 7) // 8 * 8) == (width, height)))
                        and (logical_box is None or logical == logical_box))
        if marker in (0, 216) or 208 <= marker <= 215 or at + 2 > size:
            return False
        length = int.from_bytes(data[at:at + 2], 'big')
        if length < 2 or length > size - at:
            return False
        if marker == 239 and length >= 10 and data[at + 2:at + 10] == b'MNETCROP':
            if length != 14 or crop is not None or scan:
                return False
            crop = (int.from_bytes(data[at + 10:at + 12], 'big'), int.from_bytes(data[at + 12:at + 14], 'big'))
        if marker == 238 and length >= 14 and data[at + 2:at + 7] == b'Adobe' and data[at + 13] != 1:
            return False
        if 192 <= marker <= 207 and marker not in (196, 200, 204):
            if marker != 192 or frame or length != 17 or data[at + 2] != 8 or data[at + 7] != 3:
                return False
            height = int.from_bytes(data[at + 3:at + 5], 'big')
            width = int.from_bytes(data[at + 5:at + 7], 'big')
            if not width or not height or max(width, height) > 4096 or width * height > 2048 * 2048 or (width * height) % 8:
                return False
            if data[at + 8] != 1 or data[at + 9] not in (0x11, 0x21, 0x22) or data[at + 11] != 2 or data[at + 12] != 0x11 or data[at + 14] != 3 or data[at + 15] != 0x11:
                return False
            frame = True
        if marker == 218:
            if not frame or scan or length != 12 or data[at + 2] != 3 or data[at + 3:at + 8:2] != b'\x01\x02\x03' or data[at + length - 3:at + length] != b'\x00\x3f\x00':
                return False
            if any(data[at + offset] >> 4 > 3 or data[at + offset] & 15 > 3 for offset in (4, 6, 8)):
                return False
            scan = True
            at += length
            while at < size:
                if data[at] != 255:
                    at += 1
                    continue
                start = at
                at += 1
                while at < size and data[at] == 255:
                    at += 1
                if at >= size:
                    return False
                if data[at] == 0 or 208 <= data[at] <= 215:
                    at += 1
                    continue
                at = start
                break
            continue
        at += length
    return False


class PhotoShow:
    """Per-screen playback and bounded, finished-frame cache. Originals are only read."""
    def __init__(self, folder=FOLDER, clock=time.monotonic, rng=None):
        import asyncio
        from collections import OrderedDict
        self.album = Photos(folder, clock, rng)
        self.frames = OrderedDict()
        self.lock = asyncio.Lock()
        self.current_paths = ()
        self.since = None
        self.settings = None
        self.ahead = None
        self.prefetch = None
        self.metadata = {}
        self.reservation_links = []
        self.reservation = None
        self.reservation_session = None
        self.reservation_signature = None
        self.reservation_commits = OrderedDict()
        self.retired_sessions = OrderedDict()
        self.advance_serials = OrderedDict()  # Bounded per-session manual-next high-water marks.
        import threading
        self.render_lock = threading.Lock()
        self.state_lock = threading.RLock()

    def describe(self, max_mb=8, force=False):
        with self.state_lock:
            return self._describe(max_mb, force)

    def _describe(self, max_mb=8, force=False):
        from PIL import Image
        self.album.scan(force=force)
        entries = []
        for path in self.album.files:
            reason, portrait = '', False
            try:
                st = os.stat(path)
                key = (st.st_mtime_ns, st.st_size)
                if st.st_size > max_mb * 1024 * 1024:
                    reason = 'size_limit'
                else:
                    cached = self.metadata.get(path)
                    if not cached or cached[0] != key:
                        self.album.skipped.discard(path)
                        try:
                            with Image.open(path) as im:
                                width, height = im.size
                                if im.getexif().get(274) in (5, 6, 7, 8):
                                    width, height = height, width
                                cached = (key, 'too_many_pixels' if width * height > MAX_PIXELS else '', height > width)
                        except Exception:
                            cached = (key, 'unreadable', False)
                        self.metadata[path] = cached
                    _, reason, portrait = cached
                if path in self.album.skipped:
                    reason = 'unreadable'
            except OSError:
                reason = 'unreadable'
            entries.append({'name': os.path.relpath(path, self.album.folder), 'reason': reason, 'portrait': portrait})
        self.metadata = {p: v for p, v in self.metadata.items() if p in self.album.files}
        return {'exists': os.path.isdir(self.album.folder), 'total': len(entries),
                'usable': sum(not e['reason'] for e in entries),
                'portraits': sum(not e['reason'] and e['portrait'] for e in entries),
                'skipped': [e for e in entries if e['reason']]}

    def candidates(self, settings):
        with self.state_lock:
            return self._candidates(settings)

    def _candidates(self, settings):
        report = self.describe(settings.get('photo_mb', 8))
        bad = {e['name'] for e in report['skipped']}
        paths = [p for p in self.album.files if os.path.relpath(p, self.album.folder) not in bad]
        if settings.get('photo_fit', 'fill') == 'pair':
            portraits = [p for p in paths if self.metadata.get(p, (None, '', False))[2]]
            if portraits:
                paths = portraits
        return sorted(paths, key=lambda p: os.path.relpath(p, self.album.folder).casefold())

    def next_paths(self, settings):
        with self.state_lock:
            return self._next_paths(settings)

    def _next_paths(self, settings):
        available = self.candidates(settings)
        self.album.order = [p for p in self.album.order if p in available]
        count = min(2 if settings.get('photo_fit') == 'pair' else 1, len(available))
        selected = []
        for _ in range(count):
            if not self.album.order:
                self.album.order = list(available)
                if settings.get('photo_order', 'shuffle') == 'shuffle':
                    self.album.rng.shuffle(self.album.order)
                    if len(available) > count and self.album.order[0] in self.current_paths:
                        self.album.order = self.album.order[1:] + self.album.order[:1]
            path = next((p for p in self.album.order if p not in selected), '')
            if path:
                self.album.order.remove(path)
                selected.append(path)
        return tuple(selected)

    def render(self, paths, box, fit, rle=False, jpeg=False):
        with self.render_lock:
            return self._render(paths, box, fit, rle, jpeg)

    def _render(self, paths, box, fit, rle=False, jpeg=False):
        from PIL import Image, ImageOps
        import tile_art
        if not paths:
            raise FileNotFoundError('no readable photos')
        signatures = tuple((p, os.stat(p).st_mtime_ns, os.stat(p).st_size) for p in paths)
        key = (signatures, box, fit, rle, jpeg)
        if key in self.frames:
            self.frames.move_to_end(key)
            return self.frames[key]
        # Retain an already suitable RGB baseline JPEG, including its original quality.
        # Orientation and colour profiles require conversion, and pair/letterbox/crop require composition.
        if jpeg and len(paths) == 1:
            with Image.open(paths[0]) as source:
                if source.format == 'JPEG' and source.mode == 'RGB' and source.size == box \
                        and not source.info.get('progressive') and not source.info.get('progression') \
                        and source.getexif().get(274, 1) == 1 and not source.info.get('icc_profile') \
                        and getattr(source, 'bits', 8) == 8:
                    try:
                        source.load()  # Do not pass through a truncated or otherwise unreadable JPEG.
                    except (OSError, ValueError):
                        with self.state_lock:
                            self.album.skipped.add(paths[0])
                        raise ValueError('unreadable photo') from None
                    data = Path(paths[0]).read_bytes()
                    if baseline_jpeg(data, box):
                        result = (data, data)
                        self._cache(key, result)
                        return result
        canvas = Image.new('RGB', box)
        width, height = box
        gap = max(2, round(width / 160)) if len(paths) == 2 else 0
        frame_w = (width - gap) // 2 if len(paths) == 2 else width
        for i, path in enumerate(paths):
            try:
                with Image.open(path) as source:
                    if source.width * source.height > MAX_PIXELS:
                        raise ValueError('too many pixels')
                    source.draft('RGB', (frame_w, height))
                    image = ImageOps.exif_transpose(source).convert('RGB')
                    bounds = (frame_w if i == 0 else width - frame_w - gap, height)
                    if fit == 'fill' and len(paths) == 1:
                        image = ImageOps.fit(image, bounds, Image.Resampling.LANCZOS)
                    else:
                        image.thumbnail(bounds, Image.Resampling.LANCZOS)
                    x = (0 if i == 0 else frame_w + gap) + (bounds[0] - image.width) // 2
                    canvas.paste(image, (x, (height - image.height) // 2))
            except Exception:
                with self.state_lock:
                    self.album.skipped.add(path)
                raise ValueError('unreadable photo') from None
        out = io.BytesIO()
        encoded = canvas
        padded_box = tuple((n + 7) // 8 * 8 for n in box) if jpeg else box
        if padded_box != box:
            # Hardware requires an aligned encoded frame. Preserve every logical pixel and replicate only
            # the final row/column into padding which the native decoder excludes from the LVGL picture.
            encoded = Image.new('RGB', padded_box)
            encoded.paste(canvas, (0, 0))
            if padded_box[0] > width:
                encoded.paste(canvas.crop((width - 1, 0, width, height)).resize((padded_box[0] - width, height)), (width, 0))
            if padded_box[1] > height:
                encoded.paste(encoded.crop((0, height - 1, padded_box[0], height)).resize((padded_box[0], padded_box[1] - height)), (0, height))
        encoded.save(out, 'JPEG', quality=95 if jpeg else 85, subsampling=0 if jpeg else 2, progressive=False, optimize=True)
        data = out.getvalue()
        if padded_box != box:
            payload = b'MNETCROP' + width.to_bytes(2, 'big') + height.to_bytes(2, 'big')
            data = data[:2] + b'\xff\xef' + (len(payload) + 2).to_bytes(2, 'big') + payload + data[2:]
        use_jpeg = jpeg and baseline_jpeg(data, box)
        if jpeg and not use_jpeg:
            LOG.info('photos JPEG outside decoder bounds (%d bytes); using BMP at the same resolution', len(data))
        result = (data if use_jpeg else tile_art.bmp(canvas, compact=True, rle=rle), data)
        self._cache(key, result)
        return result

    def _cache(self, key, result):
        self.frames[key] = result
        # Full-screen frames can be large. Keep at most four and at most 16 MB per active screen.
        while len(self.frames) > 4 or sum(len(a) + len(b) for a, b in self.frames.values()) > 16 * 1024 * 1024:
            self.frames.popitem(last=False)

    async def warm(self, settings, box, rle=False, jpeg=False):
        """Prepare the first frame while the ordinary page is showing, without starting its display interval."""
        import asyncio
        loop=asyncio.get_running_loop()
        async with self.lock:
            signature=(settings.get('photo_fit','fill'),settings.get('photo_order','shuffle'),settings.get('photo_mb',8),box,rle,jpeg)
            if self.settings == signature and (self.current_paths or self.ahead or self.reservation):
                return
            if self.prefetch:
                await self.prefetch
                self.prefetch=None
            self.settings,self.current_paths,self.since=signature,(),None
            self.album.order=[]
            self.ahead=await loop.run_in_executor(None,self.next_paths,settings)
            if self.ahead:
                await loop.run_in_executor(None,self.render,self.ahead,box,signature[0],rle,jpeg)

    async def reserve(self, settings, box, session, commit='', rle=False, jpeg=False):
        """One immutable pending frame. Only acknowledging its id moves the visible cursor.

        GET and duplicate/out-of-order acknowledgements never consume another photo.
        A fresh saver session retires the previous session, including its late replies.
        """
        import asyncio
        import secrets
        loop = asyncio.get_running_loop()
        if not isinstance(session, str) or not session or len(session) > 160:
            raise ValueError('invalid photo preparation session')
        if not isinstance(commit, str) or len(commit) > 128:
            raise ValueError('invalid photo preparation acknowledgement')
        async with self.lock:
            if session in self.retired_sessions:
                raise ValueError('retired photo preparation session')
            # Re-stat the selected album on each reservation request. Files themselves stay untouched.
            await loop.run_in_executor(None, self.describe, settings.get('photo_mb', 8), True)
            files = tuple((p, os.stat(p).st_mtime_ns, os.stat(p).st_size)
                          for p in self.album.files if os.path.isfile(p))
            signature = (repr(sorted(settings.items())), box, rle, jpeg, files)
            reset = session != self.reservation_session or signature != self.reservation_signature
            if reset:
                if session != self.reservation_session and self.reservation_session:
                    self.retired_sessions[self.reservation_session] = True
                    while len(self.retired_sessions) > 32:
                        self.retired_sessions.popitem(last=False)
                if self.prefetch:
                    await self.prefetch
                    self.prefetch = None
                self.reservation_session, self.reservation_signature = session, signature
                self.reservation, self.reservation_commits = None, {}
                self.current_paths, self.ahead, self.since = (), None, None
                self.settings = (settings.get('photo_fit', 'fill'), settings.get('photo_order', 'shuffle'),
                                 settings.get('photo_mb', 8), box, rle, jpeg)
                self.album.order = []
            if commit:
                if commit in self.reservation_commits:
                    pass  # A duplicate acknowledges no additional frame.
                elif self.reservation and commit == self.reservation['id']:
                    self.current_paths = self.reservation['paths']
                    self.since = self.album.clock()
                    self.reservation_commits[commit] = True
                    while len(self.reservation_commits) > 32:
                        del self.reservation_commits[next(iter(self.reservation_commits))]
                    self.reservation = None
                elif not reset:
                    raise ValueError('unknown photo preparation acknowledgement')
                # Changed files/settings invalidate the old id, then issue a fresh reservation.
            if self.reservation is None:
                order, rng_state = list(self.album.order), self.album.rng.getstate()
                def prepare():
                    paths = self.next_paths(settings)
                    image, _ = self.render(paths, box, settings.get('photo_fit', 'fill'), rle, jpeg)
                    return paths, image
                worker = loop.run_in_executor(None, prepare)
                try:
                    paths, image = await asyncio.shield(worker)
                except (OSError, ValueError, asyncio.CancelledError):
                    # Cancellation cannot stop an executor thread. Finish it before restoring its cursor,
                    # so a late worker cannot mutate the next request's selection after releasing the lock.
                    try:
                        await worker
                    except (OSError, ValueError, asyncio.CancelledError):
                        pass
                    self.album.order = order
                    self.album.rng.setstate(rng_state)
                    raise
                self.reservation = {'id': secrets.token_hex(12), 'paths': paths, 'image': image}
            return self.reservation['id'], self.reservation['image']

    async def advance(self, request_key):
        """Expire once per gesture, under the same lock as the playback cursor.

        Cancellation while waiting consumes nothing. Once applied, a retried or
        interleaved duplicate cannot expire another frame, even after its GET.
        Prepared frames, ordinary timers and the transfer path stay unchanged.
        """
        async with self.lock:
            session, raw_serial = request_key
            try:
                serial = int(raw_serial)
            except (TypeError, ValueError):
                return False
            if not session or not 0 < serial <= 0xffffffff:
                return False
            previous = self.advance_serials.get(session)
            if previous is not None and not 0 < ((serial - previous) & 0xffffffff) < 0x80000000:
                return False
            self.since = None
            self.advance_serials[session] = serial
            self.advance_serials.move_to_end(session)
            while len(self.advance_serials) > 32:
                self.advance_serials.popitem(last=False)
            return True

    async def frame(self, settings, box, preview=False, rle=False, jpeg=False):
        import asyncio
        loop = asyncio.get_running_loop()
        async with self.lock:
            if preview:
                paths = tuple((await loop.run_in_executor(None, self.candidates, settings))[:2 if settings.get('photo_fit') == 'pair' else 1])
                return await loop.run_in_executor(None, self.render, paths, box, settings.get('photo_fit', 'fill'))
            signature = (settings.get('photo_fit', 'fill'), settings.get('photo_order', 'shuffle'), settings.get('photo_mb', 8), box, rle, jpeg)
            if signature != self.settings:
                self.settings, self.current_paths, self.since, self.ahead = signature, (), None, None
                self.album.order = []
                if self.prefetch:
                    await self.prefetch
                    self.prefetch = None
            due = self.since is None or self.album.clock() - self.since >= settings.get('photo_seconds', 30)
            if due or any(not os.path.isfile(p) for p in self.current_paths):
                if self.prefetch:
                    await self.prefetch
                    self.prefetch = None
                self.current_paths = self.ahead or await loop.run_in_executor(None, self.next_paths, settings)
                self.ahead = None
                self.since = self.album.clock()
            try:
                result = await loop.run_in_executor(None, self.render, self.current_paths, box, signature[0], rle, jpeg)
            except (OSError, ValueError):
                self.since, self.ahead = None, None
                raise
            if self.ahead is None:
                self.ahead = await loop.run_in_executor(None, self.next_paths, settings)
                if self.ahead:
                    async def prepare(paths=self.ahead, bounds=box, fit=signature[0], packed=rle, compressed=jpeg):
                        try:
                            await loop.run_in_executor(None, self.render, paths, bounds, fit, packed, compressed)
                        except (OSError, ValueError):
                            pass  # describe reports the failed file; the next request can choose another
                    self.prefetch = asyncio.create_task(prepare())
            return result
