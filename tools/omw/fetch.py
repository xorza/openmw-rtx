"""Downloads, each written beside its final name and given it only once its checksum has passed: a
run cut off mid-way, or a mirror that served something else, leaves nothing a later run mistakes for
the file."""

import hashlib
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

from omw.pins import Pin
from omw.system import WINDOWS, Refusal, run


class _HttpsOnly(urllib.request.HTTPRedirectHandler):
    """TLS for every hop: a redirect to plain HTTP is refused, as `curl --proto =https` refused it."""

    def redirect_request(self, request, fp, code, message, headers, url):
        if not url.startswith("https://"):
            raise Refusal(f"{request.full_url} redirects to {url}, which is not HTTPS")
        return super().redirect_request(request, fp, code, message, headers, url)


_opener = urllib.request.build_opener(_HttpsOnly)
# A name for the driver, since LunarG answers Python's own with 403 Forbidden.
_opener.addheaders = [("User-Agent", "omw (https://github.com/xorza/openmw-rtx)")]


def partial_of(path: Path) -> Path:
    return path.with_name(path.name + ".partial")


def digest_of(path: Path, algorithm: str) -> str:
    with open(path, "rb") as file:
        return hashlib.file_digest(file, algorithm).hexdigest()


def download(url: str, file: Path, *, sha256: str | None = None, sha512: str | None = None) -> None:
    """`url` into `file`, checked against the digest given; three tries against a network or a server
    that fails once in a while, and one against an answer that says the file is not there, or a
    checksum that fails."""
    if not url.startswith("https://"):
        raise Refusal(f"{url} is not HTTPS")
    print(f"fetching {file.name}", file=sys.stderr)
    file.parent.mkdir(parents=True, exist_ok=True)
    partial = partial_of(file)
    for attempt in range(3):
        try:
            with _opener.open(url, timeout=60) as response, open(partial, "wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
            break
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            refused = isinstance(error, urllib.error.HTTPError) and error.code < 500
            if refused or attempt == 2:
                partial.unlink(missing_ok=True)
                raise Refusal(f"could not fetch {url}: {error}") from error
            time.sleep(2 ** attempt)

    settle(partial, file, url, sha256=sha256, sha512=sha512)


def settle(partial: Path, file: Path, url: str, *, sha256: str | None = None, sha512: str | None = None) -> None:
    """A download given its final name once each digest asked for has passed, and removed where one
    has not."""
    for algorithm, expected in (("sha256", sha256), ("sha512", sha512)):
        if expected is None:
            continue
        found = digest_of(partial, algorithm)
        if found != expected.lower():
            partial.unlink()
            raise Refusal(f"{url} has the {algorithm} {found}, and {expected} is pinned")
    partial.replace(file)


def download_pin(pin: Pin, file: Path) -> None:
    download(pin.url, file, sha256=pin.sha256)


def seven_zip() -> Path:
    """7-Zip, the one tool the SDK installers do not bring, for the .7z set and LLVM's NSIS package;
    looked for where its installer puts it."""
    found = shutil.which("7z")
    path = Path(found) if found else Path(r"C:\Program Files\7-Zip\7z.exe")
    if not path.is_file():
        raise Refusal("the archives are .7z and NSIS, and there is no 7-Zip: `winget install 7zip.7zip`")
    return path


def unpack_7z(archive: Path, into: Path, *members: str, flat: bool = False) -> None:
    run([seven_zip(), "e" if flat else "x", "-y", f"-o{into}", archive, *members], stdout=subprocess.DEVNULL)


def extract_member(archive: Path, name: str, into: Path) -> Path:
    """The one file called `name` inside a .zip or .tar.xz, wherever in it, into `into` under its own
    name."""
    into.mkdir(parents=True, exist_ok=True)
    target = into / name
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as opened:
            member = next((m for m in opened.namelist() if PurePosixPath(m).name == name), None)
            if member is None:
                raise Refusal(f"{archive.name} holds no {name}")
            with opened.open(member) as source, open(target, "wb") as out:
                shutil.copyfileobj(source, out)
    else:
        with tarfile.open(archive) as opened:
            found = next((m for m in opened.getmembers() if m.isfile() and PurePosixPath(m.name).name == name), None)
            if found is None:
                raise Refusal(f"{archive.name} holds no {name}")
            extracted = opened.extractfile(found)
            assert extracted is not None, "a regular member always has contents"
            with extracted, open(target, "wb") as out:
                shutil.copyfileobj(extracted, out)
    if not WINDOWS:
        target.chmod(0o755)
    return target
