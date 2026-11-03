# Kokoro-with-alignment sidecar. CPU-only on purpose: an 82M model runs at ~6x realtime on
# this Threadripper, so pulling in a multi-GB ROCm torch stack would add size and fragility
# for no measurable gain.
FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends espeak-ng \
 && rm -rf /var/lib/apt/lists/*
# `misaki[en]` also installs spacy-curated-transformers, which pulls the full PyTorch/CUDA
# stack even though this service explicitly creates G2P(trf=False). Install its CPU G2P
# dependencies directly; the unused CUDA wheels cost over 1 GB and slow recovery.
RUN pip install --no-cache-dir onnxruntime numpy huggingface_hub misaki spacy \
    espeakng-loader num2words phonemizer-fork fastapi uvicorn pydantic
COPY app.py /app/app.py
WORKDIR /app
EXPOSE 13400
CMD ["uvicorn", "app:app", "--host", "0.0.0.0", "--port", "13400"]
