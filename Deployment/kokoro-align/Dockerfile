# syntax=docker/dockerfile:1.27.1@sha256:4edf897a3ffa55b89f906fc8cc78afdb3f1834cc9c7083565e611a8a7d5fe99e
# Kokoro-with-alignment sidecar. CPU-only on purpose: an 82M model runs at ~6x realtime on
# this Threadripper, so pulling in a multi-GB ROCm torch stack would add size and fragility
# for no measurable gain.
#
# Pinned by digest: python:3.12-slim as it resolved on 2026-11-12. Stay on 3.12 -- misaki
# declares Requires-Python <3.13.
FROM python:3.12.16-slim-trixie@sha256:5e2bdb1b0d542d5039cffb8181c65930093949194a65c365dfb1b0fe13a4fa5b
RUN apt-get update && apt-get install -y --no-install-recommends espeak-ng \
 && rm -rf /var/lib/apt/lists/*
# Python dependencies come from a LOCK: every package, transitive ones included, at an exact
# version with its sha256, so pip refuses anything that is not byte-for-byte what was resolved.
# requirements-direct.txt says what app.py needs and why (misaki without [en], which would pull
# the full PyTorch/CUDA stack; spaCy's en_core_web_sm baked in rather than downloaded at runtime);
# requirements.txt is compiled from it, and its header has the command. CI installs the same lock
# and smoke-tests app.py against it (.github/workflows/checks.yml).
#
# One input is still fetched unpinned: docopt (via num2words) ships only an sdist, so pip builds
# it with whatever setuptools its isolated build environment downloads. What that build installs
# is the same single pure-Python module either way.
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir --require-hashes -r /tmp/requirements.txt \
 && rm /tmp/requirements.txt
COPY app.py /app/app.py
WORKDIR /app
EXPOSE 13400
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "13400"]
