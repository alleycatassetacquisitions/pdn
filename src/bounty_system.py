# Existing code...

def get_bounty_light_pattern(bounty_status, time_of_day):
    pattern = update_bounty_light_pattern(bounty_status, time_of_day)
    if pattern == "alternate_day_pattern":
        return alternate_day_pattern()
    else:
        # Existing night pattern
        return night_pattern()

# Existing code...