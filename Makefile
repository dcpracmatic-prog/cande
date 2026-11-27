.PHONY: test python-test cpp-build cpp-test clean

PYTHON ?= python3

# Ejecuta todos los controles en orden: compilar, pruebas Python (incluido el
# diferencial real sobre grafos compartidos) y pruebas propias de C++.
test:
	$(MAKE) -C cpp
	PYTHONPATH=src $(PYTHON) -m pytest -q
	$(MAKE) -C cpp test

python-test:
	PYTHONPATH=src $(PYTHON) -m pytest -q

cpp-build:
	$(MAKE) -C cpp

cpp-test:
	$(MAKE) -C cpp test

clean:
	$(MAKE) -C cpp clean
	rm -rf .pytest_cache src/*.egg-info src/ce/__pycache__ tests/__pycache__
