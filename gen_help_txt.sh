#!/bin/bash

mkdir -p tmp
cp README.md tmp/README.md

# Process each help text section
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

mv tmp/README.md README.md
rm -rf tmp

echo "Help text sections have been updated in README.md"

