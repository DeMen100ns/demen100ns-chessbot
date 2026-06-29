using Chess.Core;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace CodingAdventureBot;

public static class BenchPrune
{
    static string Trim(string value)
    {
        return value?.Trim() ?? string.Empty;
    }

    static bool ParsePositiveInt(string text, out int value)
    {
        return int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out value) && value > 0 && value <= 1000;
    }

    static List<string> LoadFens(string path)
    {
        using StreamReader reader = new(path);
        string? firstLine = reader.ReadLine();
        if (string.IsNullOrWhiteSpace(firstLine))
        {
            throw new InvalidOperationException("Missing count line in " + path);
        }

        if (!int.TryParse(Trim(firstLine), NumberStyles.None, CultureInfo.InvariantCulture, out int expectedCount) || expectedCount <= 0)
        {
            throw new InvalidOperationException("Invalid count line in " + path);
        }

        List<string> fens = new(expectedCount);
        string? line;
        while ((line = reader.ReadLine()) != null)
        {
            line = Trim(line);
            if (!string.IsNullOrEmpty(line))
            {
                fens.Add(line);
            }
        }

        if (fens.Count != expectedCount)
        {
            throw new InvalidOperationException($"Expected {expectedCount} FENs but found {fens.Count} in {path}");
        }

        return fens;
    }

    static void PrintHeader()
    {
        Console.WriteLine(
            $"{Pad("depth", 8)}" +
            $"{Pad("total nodes", 16)}" +
            $"{Pad("total qnodes", 16)}" +
            $"{Pad("total leaves", 16)}" +
            $"{Pad("total qleaves", 16)}" +
            $"{Pad("avg nodes/position", 20)}" +
            $"{Pad("avg qnodes/position", 20)}" +
            $"{Pad("avg leaves/position", 20)}" +
            $"{Pad("avg qleaves/position", 22)}");
    }

    static void PrintRow(int depth, Searcher.NodeStats stats, int positionCount)
    {
        double positions = positionCount;
        Console.WriteLine(
            $"{Pad(depth.ToString(CultureInfo.InvariantCulture), 8)}" +
            $"{Pad(stats.nodes.ToString(CultureInfo.InvariantCulture), 16)}" +
            $"{Pad(stats.qnodes.ToString(CultureInfo.InvariantCulture), 16)}" +
            $"{Pad(stats.leaves.ToString(CultureInfo.InvariantCulture), 16)}" +
            $"{Pad(stats.qleaves.ToString(CultureInfo.InvariantCulture), 16)}" +
            $"{Pad((stats.nodes / positions).ToString("F2", CultureInfo.InvariantCulture), 20)}" +
            $"{Pad((stats.qnodes / positions).ToString("F2", CultureInfo.InvariantCulture), 20)}" +
            $"{Pad((stats.leaves / positions).ToString("F2", CultureInfo.InvariantCulture), 20)}" +
            $"{Pad((stats.qleaves / positions).ToString("F2", CultureInfo.InvariantCulture), 22)}");
    }

    static string Pad(string value, int width)
    {
        if (value.Length >= width)
        {
            return value;
        }
        return value + new string(' ', width - value.Length);
    }

    static Searcher.SearchToggles ParseToggles(string[] args, int startIndex)
    {
        Searcher.SearchToggles toggles = Searcher.SearchToggles.Default;
        for (int i = startIndex; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--no-mdp":
                    toggles.EnableMateDistancePruning = false;
                    break;
                case "--no-tt":
                    toggles.EnableTranspositionTable = false;
                    break;
                case "--no-order":
                    toggles.EnableMoveOrdering = false;
                    break;
                case "--no-ext":
                    toggles.EnableExtensions = false;
                    break;
                case "--no-lmr":
                    toggles.EnableLateMoveReductions = false;
                    break;
                case "--no-kh":
                    toggles.EnableKillerHistoryUpdates = false;
                    break;
                default:
                    throw new InvalidOperationException("Unknown bench flag: " + args[i]);
            }
        }
        return toggles;
    }

    static string DescribeToggles(Searcher.SearchToggles toggles)
    {
        List<string> disabled = new();
        if (!toggles.EnableMateDistancePruning) disabled.Add("mdp");
        if (!toggles.EnableTranspositionTable) disabled.Add("tt");
        if (!toggles.EnableMoveOrdering) disabled.Add("order");
        if (!toggles.EnableExtensions) disabled.Add("ext");
        if (!toggles.EnableLateMoveReductions) disabled.Add("lmr");
        if (!toggles.EnableKillerHistoryUpdates) disabled.Add("kh");
        return disabled.Count == 0 ? "all-on" : "disabled=" + string.Join(",", disabled);
    }

    public static int Run(string[] args)
    {
        if (args.Length < 4 ||
            !ParsePositiveInt(args[0], out int minDepth) ||
            !ParsePositiveInt(args[1], out int maxDepth) ||
            !ParsePositiveInt(args[2], out int maxPositions) ||
            minDepth > maxDepth)
        {
            Console.Error.WriteLine("Usage: --bench-prune <min_depth> <max_depth> <positions> <fen_file> [--no-mdp] [--no-tt] [--no-order] [--no-ext] [--no-lmr] [--no-kh]");
            return 1;
        }

        string fenPath = args[3];
        try
        {
            Searcher.SearchToggles toggles = ParseToggles(args, 4);
            List<string> fens = LoadFens(fenPath);
            if (maxPositions > fens.Count)
            {
                throw new InvalidOperationException($"Requested {maxPositions} positions but only {fens.Count} are available in {fenPath}");
            }
            if (maxPositions < fens.Count)
            {
                fens = fens.GetRange(0, maxPositions);
            }

            Console.WriteLine($"Loaded {fens.Count} positions from {fenPath}");
            Console.WriteLine($"Toggles: {DescribeToggles(toggles)}");
            PrintHeader();

            for (int depth = minDepth; depth <= maxDepth; depth++)
            {
                Searcher.NodeStats total = new();
                foreach (string fen in fens)
                {
                    Board board = Board.CreateBoard(fen);
                    Searcher searcher = new(board);
                    searcher.Toggles = toggles;
                    Searcher.NodeStats stats = searcher.SearchToDepth(depth);
                    total.nodes += stats.nodes;
                    total.qnodes += stats.qnodes;
                    total.leaves += stats.leaves;
                    total.qleaves += stats.qleaves;
                }

                PrintRow(depth, total, fens.Count);
            }
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("bench_prune error: " + ex.Message);
            return 1;
        }
    }
}
