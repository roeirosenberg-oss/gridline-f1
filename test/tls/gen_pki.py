#!/usr/bin/env python3
"""Generate a test PKI that reproduces the cross-signed-root situation and
write the bundle in the firmware format. Output into the directory argv[1]."""
import datetime, os, struct, sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import NameOID

out = sys.argv[1]
now = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)

def name(cn): return x509.Name([x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Test"), x509.NameAttribute(NameOID.COMMON_NAME, cn)])
def rsa_key(): return rsa.generate_private_key(public_exponent=65537, key_size=2048)
def cert(subject, key, issuer, issuer_key, ca, san=None):
    b = (x509.CertificateBuilder().subject_name(subject).issuer_name(issuer).public_key(key.public_key())
         .serial_number(x509.random_serial_number()).not_valid_before(now).not_valid_after(now + datetime.timedelta(days=3650))
         .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True))
    if ca:
        b = b.add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
    if san:
        b = b.add_extension(x509.SubjectAlternativeName([x509.DNSName(san)]), critical=False)
    return b.sign(issuer_key, hashes.SHA256())

# trusted (bundled) roots
kA = rsa_key(); rootA = cert(name("Services Root G2"), kA, name("Services Root G2"), kA, True)
kE = ec.generate_private_key(ec.SECP384R1()); rootE = cert(name("EC Root X2"), kE, name("EC Root X2"), kE, True)
# old root that is NOT in the bundle (like "Starfield Class 2")
kB = rsa_key(); rootB = cert(name("Old Class 2 Authority"), kB, name("Old Class 2 Authority"), kB, True)
# cross-signed copy of rootA issued by rootB (same subject + key as rootA)
crossA = cert(name("Services Root G2"), kA, name("Old Class 2 Authority"), kB, True)
kI = rsa_key(); inter = cert(name("Amazon RSA 2048 M02"), kI, name("Services Root G2"), kA, True)
kL = rsa_key(); leaf = cert(name("api.example.test"), kL, name("Amazon RSA 2048 M02"), kI, False, "api.example.test")
# ECDSA chain under rootE (issuer lookup path)
kIe = ec.generate_private_key(ec.SECP256R1()); interE = cert(name("E6"), kIe, name("EC Root X2"), kE, True)
kLe = ec.generate_private_key(ec.SECP256R1()); leafE = cert(name("api.example.test"), kLe, name("E6"), kIe, False, "api.example.test")
# attacker: same subject as rootA but its own key, issued by rootB
kX = rsa_key(); evilCross = cert(name("Services Root G2"), kX, name("Old Class 2 Authority"), kB, True)
kXi = rsa_key(); evilInter = cert(name("Amazon RSA 2048 M02"), kXi, name("Services Root G2"), kX, True)
kXl = rsa_key(); evilLeaf = cert(name("api.example.test"), kXl, name("Amazon RSA 2048 M02"), kXi, False, "api.example.test")
# self-signed leaf
kS = rsa_key(); selfLeaf = cert(name("api.example.test"), kS, name("api.example.test"), kS, False, "api.example.test")

def pem(*cs): return b"".join(c.public_bytes(serialization.Encoding.PEM) for c in cs)
chains = {
    "aws_style_cross": pem(leaf, inter, crossA),          # leaf, M02, cross-signed root (top issuer not trusted)
    "aws_style_cross_plus_old": pem(leaf, inter, crossA, rootB),  # old untrusted root also sent
    "plain_chain": pem(leaf, inter),                       # top issuer is a bundled root (original rule)
    "ecdsa_chain": pem(leafE, interE),
    "evil_same_name_other_key": pem(evilLeaf, evilInter, evilCross),
    "untrusted_root_only": pem(leaf, inter, crossA)[0:0] + pem(evilLeaf, evilInter),
    "self_signed": pem(selfLeaf),
    "evil_plus_old_root": pem(evilLeaf, evilInter, evilCross, rootB),
}
for k, v in chains.items():
    open(os.path.join(out, k + ".pem"), "wb").write(v)
entries = []
for c in (rootA, rootE):
    n = c.subject.public_bytes()
    kd = c.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    entries.append((n, kd))
entries.sort(key=lambda e: e[0])
blob = struct.pack(">H", len(entries)) + b"".join(struct.pack(">HH", len(n), len(k)) + n + k for n, k in entries)
open(os.path.join(out, "bundle.bin"), "wb").write(blob)
print("pki written:", ", ".join(chains))
