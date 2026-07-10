from pathlib import Path

import yfinance as yf


TICKERS = ["AAPL", "MSFT", "SPY", "TSLA", "BTC-USD"]
START = "2020-01-01"
END = "2025-12-31"


def main() -> None:
    data_dir = Path(__file__).resolve().parents[1] / "data"
    data_dir.mkdir(parents=True, exist_ok=True)

    for ticker in TICKERS:
        print(f"Downloading {ticker}...")
        df = yf.download(ticker, start=START, end=END, auto_adjust=False, progress=False)
        if df.empty:
            print(f"  No data returned for {ticker}")
            continue

        if isinstance(df.columns, type(df.columns)) and getattr(df.columns, "nlevels", 1) > 1:
            df.columns = df.columns.get_level_values(0)

        df = df.reset_index()
        df = df.rename(columns={"Date": "Date"})
        df = df[["Date", "Open", "High", "Low", "Close", "Volume"]]
        df["Date"] = df["Date"].dt.strftime("%Y-%m-%d")
        output = data_dir / f"{ticker}.csv"
        df.to_csv(output, index=False)
        print(f"  Saved {output}")


if __name__ == "__main__":
    main()

