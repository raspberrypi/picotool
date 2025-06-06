#!/bin/bash

ALLOWED_MISSING_COMMANDS="version"

mkdir -p tmp
cp README.md tmp/README.md

# Update each help text section with the current text
while IFS= read -r line; do
    if [[ $line =~ ^\$[[:space:]]*picotool[[:space:]]+help ]]; then
        # Extract the commands
        cmd=$(echo "$line" | sed 's/^\$[[:space:]]*//')
        if [ ! -z "$cmd" ]; then
            echo "Running: $cmd"
            $cmd > "tmp/${cmd// /_}.txt"

            # Create the new text section
            {
                printf '```text\n%s\n' "$line"
                cat "tmp/${cmd// /_}.txt"
                printf '```'
            } > "tmp/new_section.txt"

            # Replace the old section with the new one
            escaped_line=$(echo "$line" | sed 's/[.*+?^${}()|[]/\\&/g')
            perl -i -pe '
                BEGIN { $/ = undef; $new = `cat tmp/new_section.txt`; }
                s/```text\n'"$escaped_line"'\n.*?\n```/$new/s;
            ' tmp/README.md
        fi
    fi
done < README.md


# Extract commands to check for missing help text
echo "Extracting commands from main.cpp..."

extract_commands() {
    local array_name=$1
    local prefix=""
    if [[ $array_name == *"_sub_commands" ]]; then
        prefix=${array_name%_sub_commands}
    fi
    sed -n "/vector<std::shared_ptr<cmd>> $array_name/,/};/p" main.cpp | grep -o 'new \([a-z0-9_]*\)_command' | cut -d' ' -f2 | sed 's/_command$//' | while read cmd; do
        if [[ ! -z "$prefix" ]]; then
            # Remove prefix from command and replace remaining underscores with dashes
            local cmd_without_prefix=${cmd#${prefix}_}
            echo "${prefix} ${cmd_without_prefix//_/-}"
        else
            # Replace underscores with dashes for main commands
            echo "${cmd//_/-}"
        fi
    done
}

# Extract top level commands
main_commands=$(extract_commands "commands")

# Extract sub-commands
sub_command_arrays=$(grep -Eo 'vector<std::shared_ptr<cmd>> [a-z0-9_]+_sub_commands' main.cpp | cut -d' ' -f2)
sub_commands=""
for array in $sub_command_arrays; do
    echo "Extracting commands from $array..."
    array_commands=$(extract_commands "$array")
    sub_commands+=$'\n'"$array_commands"
done

# Combine all commands
commands=$(echo -e "$main_commands$sub_commands")

echo "Checking for missing help text sections..."
missing_commands=()
while IFS= read -r cmd; do
    if [ ! -z "$cmd" ]; then
        if ! grep -q "\$ picotool help $cmd" tmp/README.md; then
            if [[ ! "$ALLOWED_MISSING_COMMANDS" =~ "$cmd" ]]; then
                missing_commands+=("$cmd")
                echo "Missing help text section for command: $cmd"
            fi
        fi
    fi
done <<< "$commands"

# If there are missing commands, add their help text sections to the end of the file
# Still fails the job later, this is just to provide the output to copy into the right place
if [ ${#missing_commands[@]} -ne 0 ]; then
    echo "Adding missing help text sections to end of file..."
    for cmd in "${missing_commands[@]}"; do
        echo "Running: picotool help $cmd"
        picotool help $cmd > "tmp/picotool_help_${cmd// /_}.txt"

        {
            printf '\n```text\n$ picotool help %s\n' "$cmd"
            cat "tmp/picotool_help_${cmd// /_}.txt"
            printf '```\n'
        } >> tmp/README.md
    done
fi

mv tmp/README.md README.md
rm -rf tmp

echo "Help text sections have been updated in README.md"

# Still fail if there are missing commands
if [ ${#missing_commands[@]} -ne 0 ]; then
    echo "Failing job due to missing help text sections"
    exit 1
fi
