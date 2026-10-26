// Original read-only CIA intake utility. No decryption, installation or GUI.
using System.Buffers.Binary;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

static class Intake
{
    const int HeaderSize = 0x2020, Limit = 30 * 1024 * 1024;
    static readonly JsonSerializerOptions Json = new() { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower };
    sealed class Invalid(string code, string message) : Exception(message)
    { public string Code { get; } = code; }
    sealed record Content(uint Id, ushort Index, ushort Type, ulong Size, bool Included, ulong Offset, string Hash)
    { public bool Encrypted => (Type & 1) != 0; }
    sealed record Cia(string TitleId, ushort TitleVersion, List<Content> Contents);
    static void Check(bool value, string code, string message)
    { if (!value) throw new Invalid(code, message); }
    static uint Le(byte[] b, int n) => BinaryPrimitives.ReadUInt32LittleEndian(b.AsSpan(n, 4));
    static ulong Le64(byte[] b, int n) => BinaryPrimitives.ReadUInt64LittleEndian(b.AsSpan(n, 8));
    static ushort Be16(byte[] b, int n) => BinaryPrimitives.ReadUInt16BigEndian(b.AsSpan(n, 2));
    static uint Be32(byte[] b, int n) => BinaryPrimitives.ReadUInt32BigEndian(b.AsSpan(n, 4));
    static ulong Be64(byte[] b, int n) => BinaryPrimitives.ReadUInt64BigEndian(b.AsSpan(n, 8));
    static ulong Align(ulong n) => checked((n + 63) / 64 * 64);
    static string Hex(byte[] b) => Convert.ToHexStringLower(b);
    static void Bounds(ulong offset, ulong size, ulong end)
    { Check(offset <= end && size <= end - offset, "invalid_bounds", "Declared data lies outside the input"); }
    static byte[] Read(FileStream f, ulong at, ulong size, int maximum = 4 * 1024 * 1024)
    {
        Bounds(at, size, (ulong)f.Length);
        Check(size <= (ulong)maximum, "metadata_limit", "Requested metadata exceeds its supported size");
        byte[] data = new byte[(int)size]; f.Position = checked((long)at); f.ReadExactly(data); return data;
    }
    static byte[] Digest(FileStream f, ulong at, ulong size)
    {
        Bounds(at, size, (ulong)f.Length); f.Position = checked((long)at);
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] block = new byte[1024 * 1024];
        while (size > 0)
        {
            int read = f.Read(block, 0, (int)Math.Min(size, (ulong)block.Length));
            Check(read > 0, "input_changed", "Input ended during hashing");
            hash.AppendData(block, 0, read); size -= (ulong)read;
        }
        return hash.GetHashAndReset();
    }
    static int Body(byte[] data)
    {
        Check(data.Length >= 4, "truncated_metadata", "Signed metadata is truncated");
        return Be32(data, 0) switch { 0x10003 => 0x240, 0x10004 => 0x140, 0x10005 => 0x80,
            _ => throw new Invalid("unsupported_signature", "Require a SHA-256 CIA metadata layout") };
    }
    static Cia Parse(FileStream f)
    {
        byte[] h = Read(f, 0, HeaderSize);
        Check(Le(h, 0) == HeaderSize && h[4] == 0 && h[5] == 0 && h[6] == 0 && h[7] == 0,
              "unsupported_cia_header", "Require a standard 0x2020/version-0 CIA");
        uint certSize = Le(h, 8), tikSize = Le(h, 12), tmdSize = Le(h, 16), metaSize = Le(h, 20);
        ulong dataSize = Le64(h, 24), certAt = Align(HeaderSize);
        Check(certSize is >= 4 and <= 16384 && tikSize is >= 4 and <= 16384 && tmdSize is >= 4 and <= 4194304 && metaSize <= 1048576,
              "metadata_limit", "CIA metadata exceeds supported bounds");
        ulong tikAt = Align(certAt + certSize), tmdAt = Align(tikAt + tikSize), dataAt = Align(tmdAt + tmdSize);
        Bounds(certAt, certSize, (ulong)f.Length); Bounds(tikAt, tikSize, (ulong)f.Length);
        Bounds(tmdAt, tmdSize, (ulong)f.Length); Bounds(dataAt, dataSize, (ulong)f.Length);
        ulong end = checked(dataAt + dataSize);
        if (metaSize > 0) { end = Align(end); Bounds(end, metaSize, (ulong)f.Length); end += metaSize; }
        Check(end == (ulong)f.Length, "trailing_cia_data", "CIA file length differs from its declared regions");
        byte[] tmd = Read(f, tmdAt, tmdSize); int body = Body(tmd);
        int info = body + 0xC4, start = info + 64 * 36;
        Check(tmd.Length >= start && tmd[body + 0x40] == 1, "invalid_tmd", "CIA TMD header is invalid");
        ushort count = Be16(tmd, body + 0x9E), version = Be16(tmd, body + 0x9C);
        Check(count > 0 && tmd.Length == start + count * 48, "invalid_tmd", "CIA TMD content count/length differs");
        string title = Hex(tmd.AsSpan(body + 0x4C, 8).ToArray());
        Check(SHA256.HashData(tmd.AsSpan(info, 64 * 36)).SequenceEqual(tmd.AsSpan(body + 0xA4, 32).ToArray()),
              "cia_metadata_hash_mismatch", "CIA content-info hash mismatch");
        bool[] covered = new bool[count];
        for (int i = 0; i < 64; i++)
        {
            int first = Be16(tmd, info + i * 36), length = Be16(tmd, info + i * 36 + 2);
            if (length == 0)
            { Check(tmd.AsSpan(info + i * 36, 36).IndexOfAnyExcept((byte)0) < 0, "invalid_tmd", "Unused TMD info must be zero"); continue; }
            Check(first + length <= count, "invalid_tmd", "TMD group exceeds content table");
            for (int j = first; j < first + length; j++)
            { Check(!covered[j], "invalid_tmd", "TMD groups overlap"); covered[j] = true; }
            Check(SHA256.HashData(tmd.AsSpan(start + first * 48, length * 48)).SequenceEqual(tmd.AsSpan(info + i * 36 + 4, 32).ToArray()),
                  "cia_metadata_hash_mismatch", "CIA content-record hash mismatch");
        }
        Check(covered.All(x => x), "invalid_tmd", "TMD groups do not cover every record");
        byte[] ticket = Read(f, tikAt, tikSize); int tb = Body(ticket);
        Check(ticket.Length >= tb + 0xA4 && Hex(ticket.AsSpan(tb + 0x9C, 8).ToArray()) == title,
              "cia_ticket_title_mismatch", "CIA ticket and TMD identify different titles");
        var ids = new HashSet<uint>(); var indices = new HashSet<ushort>(); var includedIndices = new HashSet<int>();
        var contents = new List<Content>(); ulong at = dataAt, last = dataAt;
        for (int i = 0; i < count; i++)
        {
            int n = start + i * 48; uint id = Be32(tmd, n); ushort index = Be16(tmd, n + 4), flags = Be16(tmd, n + 6);
            ulong size = Be64(tmd, n + 8); string hash = Hex(tmd.AsSpan(n + 16, 32).ToArray());
            Check(ids.Add(id) && indices.Add(index) && size > 0, "invalid_cia_content", "Duplicate or empty CIA content");
            bool included = (h[32 + index / 8] & (0x80 >> (index % 8))) != 0;
            if (included)
            {
                includedIndices.Add(index); Bounds(at, size, dataAt + dataSize);
                if ((flags & 1) == 0) Check(Hex(Digest(f, at, size)) == hash, "cia_content_hash_mismatch", "CIA plaintext content hash mismatch");
            }
            contents.Add(new(id, index, flags, size, included, included ? at : 0, hash));
            if (included) { last = at + size; at = Align(last); }
        }
        Check(includedIndices.Count > 0 && last == dataAt + dataSize, "invalid_cia_content_size", "CIA included content sizes differ");
        for (int i = 0; i < 65536; i++)
            Check(((h[32 + i / 8] & (0x80 >> (i % 8))) != 0) == includedIndices.Contains(i), "invalid_cia_bitmap", "CIA bitmap and content table differ");
        return new(title, version, contents);
    }
    static byte[] ExecutableView(FileStream f, Content main, string title, out string product, out string codeHash)
    {
        byte[] h = Read(f, main.Offset, 512);
        Check(h.AsSpan(0x100, 4).SequenceEqual("NCCH"u8), "invalid_ncch", "Main content is not NCCH");
        Check(BinaryPrimitives.ReadUInt16LittleEndian(h.AsSpan(0x112, 2)) == 2 && h[0x18E] <= 8,
              "unsupported_ncch", "Require standard version-2 NCCH");
        Check((h[0x18F] & 4) != 0, "encrypted_input", "NCCH executable is encrypted; a decrypted executable is needed");
        Check((h[0x18D] & 2) != 0 && Le(h, 0x180) == 0x400, "not_executable", "Main NCCH lacks a supported executable header");
        ulong unit = 512UL << h[0x18E], size = Le(h, 0x104) * unit;
        Check(size == main.Size && Le64(h, 0x118).ToString("x16") == title,
              "cia_ncch_identity_mismatch", "CIA and NCCH size/title identity differ");
        byte[] extended = Read(f, main.Offset + 512, 0x800);
        Check(SHA256.HashData(extended.AsSpan(0, 0x400)).SequenceEqual(h.AsSpan(0x160, 32).ToArray()), "hash_mismatch", "NCCH extended-header hash mismatch");
        ulong exeAt = Le(h, 0x1A0) * unit, exeSize = Le(h, 0x1A4) * unit, hashed = Le(h, 0x1A8) * unit;
        Check(exeAt >= 0xA00 && exeSize >= 512 && hashed > 0 && hashed <= exeSize, "invalid_exefs", "ExeFS layout/hash region is invalid");
        Bounds(exeAt, exeSize, size);
        Check(exeAt + exeSize <= Limit, "intake_size_limit", "Executable view exceeds 30 MiB");
        Check(Digest(f, main.Offset + exeAt, hashed).SequenceEqual(h.AsSpan(0x1C0, 32).ToArray()), "hash_mismatch", "ExeFS superblock hash mismatch");
        byte[] exefs = Read(f, main.Offset + exeAt, exeSize, Limit); var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var ranges = new List<(uint At, uint End)>(); codeHash = "";
        for (int i = 0; i < 10; i++)
        {
            byte[] raw = exefs.AsSpan(i * 16, 8).ToArray(); uint at = Le(exefs, i * 16 + 8), length = Le(exefs, i * 16 + 12);
            if (raw.All(x => x == 0)) { Check(at == 0 && length == 0, "invalid_exefs", "Unnamed ExeFS slot has data"); continue; }
            int zero = Array.IndexOf(raw, (byte)0); int n = zero < 0 ? 8 : zero;
            Check(zero < 0 || raw.AsSpan(zero).IndexOfAnyExcept((byte)0) < 0, "invalid_exefs", "ExeFS name padding is invalid");
            string name = Encoding.ASCII.GetString(raw, 0, n);
            Check(name.Length > 0 && name is not "." and not ".." && raw.AsSpan(0, n).ToArray().All(x =>
                  x is >= (byte)'a' and <= (byte)'z' or >= (byte)'A' and <= (byte)'Z' or >= (byte)'0' and <= (byte)'9' or (byte)'.' or (byte)'_' or (byte)'-'),
                  "invalid_exefs", "ExeFS name is unsupported");
            Check(names.Add(name) && length > 0, "invalid_exefs", "ExeFS names must be unique/nonempty"); Bounds(512UL + at, length, exeSize);
            byte[] digest = SHA256.HashData(exefs.AsSpan(checked(512 + (int)at), checked((int)length)));
            Check(digest.SequenceEqual(exefs.AsSpan(0xC0 + (9 - i) * 32, 32).ToArray()), "hash_mismatch", "ExeFS file hash mismatch");
            ranges.Add((at, checked(at + length))); if (name == ".code") codeHash = Hex(digest);
        }
        ranges.Sort((a, b) => a.At.CompareTo(b.At));
        for (int i = 1; i < ranges.Count; i++) Check(ranges[i].At >= ranges[i - 1].End, "invalid_exefs", "ExeFS file ranges overlap");
        Check(codeHash.Length > 0, "missing_code", "ExeFS lacks .code");
        byte[] view = new byte[checked((int)(exeAt + exeSize))];
        h.AsSpan(0, 0x100).Clear(); h[0x18F] |= 2;
        BinaryPrimitives.WriteUInt32LittleEndian(h.AsSpan(0x104, 4), checked((uint)((exeAt + exeSize) / unit)));
        h.AsSpan(0x130, 32).Clear(); h.AsSpan(0x190, 16).Clear(); h.AsSpan(0x1B0, 16).Clear(); h.AsSpan(0x1E0, 32).Clear();
        h.CopyTo(view, 0); extended.CopyTo(view, 512); exefs.CopyTo(view, checked((int)exeAt));
        product = Encoding.ASCII.GetString(h, 0x150, 16).Split('\0')[0]; return view;
    }
    static string Export(string input, string output)
    {
        Check(!File.Exists(output) && !Directory.Exists(output), "output_exists", "Output exists and was preserved");
        Check((File.GetAttributes(input) & FileAttributes.ReparsePoint) == 0, "unsafe_input", "Use a regular input file");
        using var file = new FileStream(input, FileMode.Open, FileAccess.Read, FileShare.Read);
        long initialSize = file.Length; DateTime initialTime = File.GetLastWriteTimeUtc(input);
        Cia cia = Parse(file); string sourceHash = Hex(Digest(file, 0, (ulong)file.Length));
        var report = new Dictionary<string, object?> { ["schema_version"] = 1, ["stage"] = "prepare-intake",
            ["source_sha256"] = sourceHash, ["source_bytes"] = file.Length, ["cia"] = cia,
            ["profile_enabled"] = false, ["console_verified"] = false, ["romfs_exported"] = false,
            ["credentials_exported"] = false, ["signature_authenticity_verified"] = false,
            ["helper"] = "original .NET CIA intake; stored ExeFS hashes verified; decompression/semantic validation remains with host intake" };
        byte[]? view = null;
        try
        {
            Check(cia.TitleId.StartsWith("00040000", StringComparison.Ordinal), "unsupported_cia_title", "Require base application CIA; updates/DLC/system titles are unsupported");
            Content? main = cia.Contents.Find(x => x.Index == 0 && x.Included);
            Check(main is not null, "missing_cia_executable", "CIA omits executable content index 0");
            Check(!main!.Encrypted, "encrypted_cia_content", "CIA executable is outer-encrypted; decrypted executable data is needed");
            view = ExecutableView(file, main, cia.TitleId, out string product, out string storedHash);
            report["identity"] = new { program_id = cia.TitleId, product_code = product };
            report["stored_code_sha256"] = storedHash;
            report["executable_view"] = new { name = "executable-view.cxi", bytes = view.Length,
                sha256 = Hex(SHA256.HashData(view)), derived_not_full_rom = true };
        }
        catch (Invalid e) when (e.Code is "encrypted_cia_content" or "encrypted_input" or "unsupported_cia_title" or "missing_cia_executable")
        { report["blocking_error"] = new { code = e.Code, message = e.Message }; }
        Check(file.Length == initialSize && File.GetLastWriteTimeUtc(input) == initialTime && Hex(Digest(file, 0, (ulong)file.Length)) == sourceHash,
              "input_changed", "Source changed during intake; no output was published");
        report["ok"] = true; report["executable_available"] = view is not null;
        string parent = Path.GetDirectoryName(output)!; Directory.CreateDirectory(parent);
        string stage = Path.Combine(parent, ".ctr2wup-intake-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(stage);
        try
        {
            File.WriteAllText(Path.Combine(stage, "intake-report.json"), JsonSerializer.Serialize(report, Json) + "\n");
            using (var archive = ZipFile.Open(Path.Combine(stage, "CTR2WUP-intake.zip"), ZipArchiveMode.Create))
            {
                archive.CreateEntryFromFile(Path.Combine(stage, "intake-report.json"), "intake-report.json");
                if (view is not null)
                {
                    using Stream entry = archive.CreateEntry("executable-view.cxi", CompressionLevel.Optimal).Open(); entry.Write(view);
                }
            }
            Check(new FileInfo(Path.Combine(stage, "CTR2WUP-intake.zip")).Length < 32 * 1024 * 1024, "intake_size_limit", "Archive exceeds upload transfer limit");
            Directory.Move(stage, output);
            return Path.Combine(output, "CTR2WUP-intake.zip");
        }
        finally { if (Directory.Exists(stage)) Directory.Delete(stage, true); }
    }
    static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        bool interactive = args.Length < 2;
        try
        {
            Console.WriteLine("CTR2WUP CIA intake — analysis export, not an injector");
            string? input = args.FirstOrDefault();
            if (input is null) { Console.WriteLine("Drag your CIA onto this EXE, or paste its path here:"); input = Console.ReadLine()?.Trim().Trim('"'); }
            Check(!string.IsNullOrWhiteSpace(input), "missing_input", "No input file selected");
            input = Path.GetFullPath(input!);
            string output = args.Length >= 2 ? Path.GetFullPath(args[1]) : input + ".ctr2wup-intake";
            string archive = Export(input, output);
            Console.WriteLine("Created: " + archive);
            Console.WriteLine("Upload CTR2WUP-intake.zip. Original CIA unchanged. No keys, tickets, certificates or RomFS assets exported.");
            if (interactive) { Console.WriteLine("Press Enter to close."); Console.ReadLine(); }
            return 0;
        }
        catch (Exception e) when (e is Invalid or IOException or UnauthorizedAccessException or OverflowException or ArgumentException)
        {
            string code = e is Invalid invalid ? invalid.Code : "intake_io";
            Console.Error.WriteLine(JsonSerializer.Serialize(new { ok = false, error = new { code, message = e.Message } }, Json));
            if (interactive) { Console.WriteLine("Press Enter to close."); Console.ReadLine(); }
            return 2;
        }
    }
}
