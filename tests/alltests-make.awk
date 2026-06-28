#!/usr/bin/awk -f
BEGIN {
    FS="=";
    file=ARGV[1];
    xlr8=ENVIRON["XLR8"];
    base=file;
    sub("[.]xlr8", "", base);
    csource=file;
    sub("[.]xlr8", ".c", csource);
    subdir=file;
    sub("/[^/]*$", "", subdir);
}
/\/\/ +[A-Z]+=/ {
    name=substr($1, match($1, "[A-Z]"));
    value=substr($0, index($0, "=") + 1);
    gsub("%x", xlr8, value);
    gsub("%f", file, value);
    gsub("%b", base, value);
    gsub("%c", csource, value);
    gsub("%d", subdir, value);
    print base "." name "=" value;
}
