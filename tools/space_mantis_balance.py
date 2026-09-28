"""Small offline balance harness for SpaceMantis.

It mirrors the *shape* of the firmware spreadsheet, not the firmware renderer.
The script is intentionally dependency-free so the repo can be sanity-checked
without PlatformIO or an ESP32.
"""
from dataclasses import dataclass
import random

CAREERS = ["hauler", "gunhand", "prospector", "rescuer", "trader", "wanderer", "depthrunner", "ghost"]

@dataclass
class Pilot:
    credits: int = 500
    hull: int = 100
    fuel: int = 100
    depth: int = 1
    bulkheads: int = 1
    stabilizer: int = 0
    lives: int = 0
    ranks: dict = None

    def __post_init__(self):
        self.ranks = {c: 0 for c in CAREERS}

    def depth_rating(self):
        return min(5, self.bulkheads + self.ranks["depthrunner"] // 3 + self.stabilizer)

    def xp(self, career, amount):
        # Same 100*(rank+1) curve used by the firmware.
        while amount >= 100 * (self.ranks[career] + 1) and self.ranks[career] < 20:
            amount -= 100 * (self.ranks[career] + 1)
            self.ranks[career] += 1
        return amount


def dive_cost(a, b):
    return max(4, min(24, int(4 + abs(a - b) * 3)))


def run(seed=1337, minutes=120):
    rng = random.Random(seed)
    p = Pilot()
    discoveries = set()
    dives = 0
    encounters = 0
    deaths = 0
    for _ in range(minutes * 60):
        # Opportunistic flight decisions: mostly fly, occasionally deal.
        if p.fuel <= 8:
            p.fuel = min(100, p.fuel + 20)
            p.credits -= 20
        if rng.random() < .11:
            encounters += 1
            threat = 1 + p.depth + rng.randrange(5)
            attack = p.ranks["gunhand"] + 1 + rng.random() * 3
            if attack > threat * .8:
                p.credits += 15 + threat * 4
                p.xp("gunhand", 8 + threat)
            else:
                p.hull -= max(1, int(threat - p.ranks["rescuer"] * .25))
        if rng.random() < .025:
            target = min(5, max(1, p.depth + rng.choice([-1, 1])))
            cost = dive_cost(p.depth, target)
            if p.fuel >= cost:
                p.fuel -= cost
                p.depth = target
                dives += 1
                p.xp("depthrunner", 6 + target * 2)
                if target >= 3 and rng.random() < .10 + target * .04:
                    discoveries.add(rng.randrange(24))
        p.fuel = max(0, p.fuel - (1 if rng.random() < .002 else 0))
        if p.hull <= 0:
            deaths += 1
            p.lives += 1
            p.hull = 100
            p.fuel = 100
            p.depth = 1
            p.credits = max(0, p.credits - 40)
    return {
        "credits": p.credits,
        "depth": p.depth,
        "depth_rating": p.depth_rating(),
        "lives": p.lives,
        "dives": dives,
        "encounters": encounters,
        "persistent_discoveries": len(discoveries),
        "careers": dict(p.ranks),
    }


if __name__ == "__main__":
    for seed in (1, 2, 3, 42, 1337):
        print(seed, run(seed))
