from functools import lru_cache
import json
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
CONFIG_DIR = BASE_DIR / "config"


def _load_json(filename: str):
    path = CONFIG_DIR / filename
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)


@lru_cache(maxsize=None)
def load_app_config():
    return _load_json("app.json")


@lru_cache(maxsize=None)
def load_ap_config():
    return _load_json("ap.json")


@lru_cache(maxsize=None)
def load_sim_config():
    return _load_json("sim.json")

