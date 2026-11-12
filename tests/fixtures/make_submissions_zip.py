"""Writes submissions.zip, the made-up zip of submissions for tst_misc (D81).

Run: python make_submissions_zip.py [output.zip]. Python's own zip writer makes it, so the
app's reader is checked against another implementation. The "drawings" are only bytes:
the test unpacks them, it does not read them as drawings.
"""
import io
import sys
import zipfile


def body(tag):  # tst_misc builds the same bytes
    return (b"AC1032 " + tag.encode() + b" ") * 20


D, S = zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w") as z:
    z.writestr(zipfile.ZipInfo("sub/"), b"")
    z.writestr("smithjane_12345_67890_lab3.dwg", body("smith"), compress_type=D)
    z.writestr("garcíajosé_123_456_part1.dwg", body("garcia"), compress_type=D)  # UTF-8 flag
    z.writestr("muXozcarlos_7_8_a.dwg", body("munoz"), compress_type=S)  # made code page 437 below
    z.writestr("__MACOSX/._smithjane_12345_67890_lab3.dwg", b"fork", compress_type=S)
    z.writestr("sub/doejohn_111_222_x.DWG", body("doe"), compress_type=D)
    z.writestr("smithjane_12345_67891_extra.zip", b"PK\x05\x06" + b"\0" * 18, compress_type=S)
    z.writestr("readme.txt", b"hello", compress_type=D)
    z.writestr("../evil_1_2_x.dwg", body("evil"), compress_type=D)
    z.writestr("broken_5_6_b.dwg", b"CORRUPT-ME-PLEASE", compress_type=S)
    z.writestr("dup/a_1_2_x.dwg", body("lower"), compress_type=D)
    z.writestr("dup/A_1_2_X.dwg", body("upper"), compress_type=D)
blob = buf.getvalue()
# A name in code page 437 without the UTF-8 flag, as older Windows zips have (0xA4 is n-tilde).
assert blob.count(b"muXozcarlos") == 2
blob = blob.replace(b"muXozcarlos", b"mu\xa4ozcarlos")
# One stored file's bytes changed after its checksum was written.
assert blob.count(b"CORRUPT-ME-PLEASE") == 1
blob = blob.replace(b"CORRUPT-ME-PLEASE", b"CORRUPT-ME-PLEASX")
with open(sys.argv[1] if len(sys.argv) > 1 else "submissions.zip", "wb") as f:
    f.write(blob)
