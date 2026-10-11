"""Check the instrumented split case, including intentional Unity failures."""
import subprocess
import sys

for height in (1, 4):
    for scenario in ("success", "verify-failure", "remove-failure", "unload-failure"):
        result = subprocess.run(
            [sys.argv[1], scenario, str(height)], capture_output=True
        )
        output = result.stdout.decode()
        assert result.returncode == 0, (scenario, output, result.stderr.decode())
        assert not result.stderr, result.stderr.decode()
        assert "\r" not in output
        if scenario == "success":
            assert output.count("    split bptr_files/temp/split/") == 1
            assert output.count("    verify bptr_files/temp/split/") == 1
        else:
            phase = scenario.split("-")[0]
            if phase == "unload":
                phase = "split"
            assert f"    {phase} bptr_files/temp/split/{height}-2.bptr:" in output
            assert "    split bptr_files/temp/split/" + str(height) + "-*.bptr: PASS" not in output
        print(f"height {height}: {scenario}: PASS")
