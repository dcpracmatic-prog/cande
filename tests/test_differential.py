"""Prueba de integración: Python y C++ reciben exactamente la misma instancia."""
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_python_cpp_same_instance_differential():
    executable = ROOT / "cpp" / "ce_mwis"
    if not executable.exists():
        pytest.skip("C++ no compilado; ejecutar `make test` desde la raíz")

    env = {**os.environ, "PYTHONPATH": str(ROOT / "src")}
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts" / "diff_python_cpp.py"),
            "--bin",
            str(executable),
            "--cases",
            "5",
            "--n",
            "12",
        ],
        cwd=ROOT,
        env=env,
        text=True,
        capture_output=True,
        timeout=120,
        check=False,
    )
    assert result.returncode == 0, result.stdout + "\n" + result.stderr
    assert "misma_instancia=True" in result.stdout
    assert "Todas las soluciones reportadas son independientes: True" in result.stdout
