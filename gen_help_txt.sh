#!/bin/bash

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
            perl -i -0777 -pe '
                BEGIN { $new = `cat tmp/new_section.txt`; }
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
            missing_commands+=("$cmd")
            echo "Missing help text section for command: $cmd"
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

# Extract help topics to check for missing help text
echo "Extracting help topics from main.cpp..."
topics=$(sed -n '/std::map<string, help_topic> help_topics {/,/^};/p' main.cpp | \
    grep -oE '"[a-zA-Z0-9_-]+"[[:space:]]*,[[:space:]]*\{' | \
    sed -E 's/^"([a-zA-Z0-9_-]+)".*/\1/')

echo "Checking for missing help topic sections..."
missing_topics=()
while IFS= read -r topic; do
    if [ ! -z "$topic" ]; then
        if ! grep -q "\$ picotool help $topic" tmp/README.md; then
            missing_topics+=("$topic")
            echo "Missing help text section for topic: $topic"
        fi
    fi
done <<< "$topics"

# Same as for missing commands above - still fails the job later, this is just to
# provide the output to copy into the right place
if [ ${#missing_topics[@]} -ne 0 ]; then
    echo "Adding missing help topic sections to end of file..."
    for topic in "${missing_topics[@]}"; do
        echo "Running: picotool help $topic"
        picotool help "$topic" > "tmp/picotool_help_${topic// /_}.txt"

        {
            printf '\n```text\n$ picotool help %s\n' "$topic"
            cat "tmp/picotool_help_${topic// /_}.txt"
            printf '```\n'
        } >> tmp/README.md
    done
fi

# Regenerate the links to documentation for picotool commands and help topics section
echo "Regenerating links to documentation for picotool commands and help topics..."
links_line=$(picotool help 2>/dev/null | \
    awk '/^COMMANDS:/{f=1;next} f && /^[A-Z]/{exit} f && /^    [a-z]/{print $1}' | \
    grep -vE "^(help)$" | \
    while IFS= read -r cmd; do
        printf '[`%s`](#%s) ' "$cmd" "$cmd"
    done | sed 's/ $//')
topic_links_line=$(echo "$topics" | while IFS= read -r topic; do
        [ -z "$topic" ] && continue
        printf '[`%s`](#%s) ' "$topic" "$topic"
    done | sed 's/ $//')
if [ -n "$topic_links_line" ]; then
    links_line="$links_line $topic_links_line"
fi
export LINKS_LINE="$links_line"
perl -i -0777 -pe '
    s{(## Links to documentation for `picotool` commands and help topics\n)[^\n]+}{$1$ENV{LINKS_LINE}};
' tmp/README.md

mv tmp/README.md README.md
rm -rf tmp

# Check that command order in README matches picotool help output
echo "Checking command order..."

order_mismatch=false

check_cmd_topic_order() {
    local label="$1" tool_order="$2" readme_order="$3"
    local fp="" fr=""
    while IFS= read -r cmd; do
        [ -z "$cmd" ] && continue
        echo "$readme_order" | grep -qx "$cmd" && fp+="$cmd"$'\n'
    done <<< "$tool_order"
    while IFS= read -r cmd; do
        [ -z "$cmd" ] && continue
        echo "$tool_order" | grep -qx "$cmd" && fr+="$cmd"$'\n'
    done <<< "$readme_order"
    if [ "$fp" != "$fr" ]; then
        echo "Order mismatch in $label!"
        echo "  picotool: $(echo "$fp" | tr '\n' ' ')"
        echo "  README:   $(echo "$fr" | tr '\n' ' ')"
        order_mismatch=true
    fi
}

# Check top-level command order
tool_top_order=$(picotool help 2>/dev/null | \
    awk '/^COMMANDS:/{f=1;next} f && /^[A-Z]/{exit} f && /^    [a-z]/{print $1}')
readme_top_order=$(grep -n '^\$ picotool help [a-z][a-z-]*$' README.md | \
    sed 's/^\([0-9]*\):.*\$ picotool help \([a-z][a-z-]*\)$/\1 \2/' | \
    sort -n | awk '{print $2}' | awk '!seen[$0]++')
check_cmd_topic_order "top-level commands" "$tool_top_order" "$readme_top_order"

# Check help topic order (help_topics is a std::map, so picotool prints topics
# alphabetically by key rather than in main.cpp source order)
tool_topic_order=$(picotool help 2>/dev/null | \
    awk '/^TOPICS:/{f=1;next} f && /^[A-Z]/{exit} f && /^    [a-z]/{print $1}')
check_cmd_topic_order "help topics" "$tool_topic_order" "$readme_top_order"

# Check sub-command order for each parent command
for array in $sub_command_arrays; do
    prefix=${array%_sub_commands}
    tool_sub_order=$(picotool help "$prefix" 2>/dev/null | \
        awk '/^SUB COMMANDS:/{f=1;next} f && /^[A-Z]/{exit} f && /^    [a-z]/{print $1}')
    readme_sub_order=$(grep -n "^\$ picotool help ${prefix} [a-z][a-z-]*$" README.md | \
        sed "s/^\([0-9]*\):.*\$ picotool help ${prefix} \([a-z][a-z-]*\)$/\1 \2/" | \
        sort -n | awk '{print $2}' | awk '!seen[$0]++')
    check_cmd_topic_order "${prefix} sub-commands" "$tool_sub_order" "$readme_sub_order"
done

echo "Help text sections have been updated in README.md"

# Still fail if there are missing commands or topics
if [ ${#missing_commands[@]} -ne 0 ] || [ ${#missing_topics[@]} -ne 0 ]; then
    echo "Failing job due to missing help text sections"
    exit 1
fi
if $order_mismatch; then
    echo "Failing job due to command order mismatch in README"
    exit 1
fi
