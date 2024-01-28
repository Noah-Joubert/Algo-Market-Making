import pandas as pd
import os
import argparse
import dash_bootstrap_components as dbc
import plotly.subplots
from dash import Dash, dcc, callback, Output, Input, html
import plotly.express as px
from enum import Enum
import plotly.graph_objects as graph_objects

PATH = ""  # path to the round data
TRADER_NAME = ""  # name of our trader

DEFAULT_TAB = "Refresh"
CURRENT_TAB = DEFAULT_TAB
TABS = {DEFAULT_TAB: html.Div()}
DEV_MODE = False

app = None


class ComponentNames(Enum):
    OUR_EXPOSURE = "Exposure"
    ALL_TRADERS_PNL = "All Traders PnL"

    TRADES_PER_SECOND = "Trades Per Second"
    CANCEL_RATIO = "Cancel Ratio"
    LOTS_PER_TRADE = "Lots Per Trade"

    TRADER_ACTIVITY = "Trader Activity"

    TAB_BUTTONS = "Tab Buttons"
    DYNAMIC_GRAPH_CHECKLIST = "Dynamic Graph Checklist"


class ChecklistOptions(Enum):
    ETF_MID = "ETF Mid"
    FUTURES_MID = "Futures Mid"
    ETF_TRADES_SENT = "ETF Trades Sent"
    ETF_TRADES_FILLED = "ETF Trades Filled"
    FUTURES_TRADES_SENT = "Futures Trades Sent"
    FUTURES_TRADES_FILLED = "Futures Trades Filled"
    ORDER_BOOK = "Order Book"
    SHOW_TRADE_TICKS = "Trade Ticks"


# todo: wack in enum
SCOREBOARD_FILE_NAME = "score_board.csv"
MATCH_EVENTS_FILE_NAME = "match_events.csv"
CUSTOM_LOG_FILE_NAME = "custom_log"
TRADES_SENT_FILE_NAME = "trades_sent.csv"
TRADES_FILLED_FILE_NAME = "trades_filled.csv"
TRADES_PNL_FILE_NAME = "trades_pnl.csv"
PRICES_FILE_NAME = "prices.csv"
ORDER_BOOK_FILE_NAME = "order_book.csv"
TRADE_TICKS_FILE_NAME = "trade_ticks.csv"


class Tab:
    # build the plots that won't be updated during runtime
    def import_data(self):
        try:
            self.scoreboard = pd.read_csv(os.path.join(self.market_data_folder_path, SCOREBOARD_FILE_NAME))
            self.scoreboard = self.scoreboard[100:]  # seems to be an issue with early scoreboard data
            self.match_events = pd.read_csv(os.path.join(self.market_data_folder_path, MATCH_EVENTS_FILE_NAME))
        except FileNotFoundError:
            print("[Error] Scoreboard or match events not found")
            pass

        custom_log_path = os.path.join(self.market_data_folder_path, CUSTOM_LOG_FILE_NAME)

        # get trades sent
        try:
            self.trades_sent = pd.read_csv(os.path.join(custom_log_path, TRADES_SENT_FILE_NAME))
        except FileNotFoundError:
            pass

        # get trades filled
        try:
            self.trades_filled = pd.read_csv(os.path.join(custom_log_path, TRADES_FILLED_FILE_NAME))
        except FileNotFoundError:
            pass

        # get trades pnl
        try:
            self.trades_pnl = pd.read_csv(os.path.join(custom_log_path, TRADES_PNL_FILE_NAME))
        except FileNotFoundError:
            pass

        # get prices
        try:
            self.prices = pd.read_csv(os.path.join(custom_log_path, PRICES_FILE_NAME))
        except FileNotFoundError:
            pass

        def get_least_competitive_price(row, side):
            # this functions returns the least competitive price from a set of bids or asks, ignoring zeroes
            for level in ['4', '3', '2', '1', '0']:
                if row[side + 'Price' + level] != 0:
                    return row[side + 'Price' + level]
            return pd.NA

        def get_total_orders(row, side):
            total = 0
            for level in ['4', '3', '2', '1', '0']:
                total += row[side + 'Vol' + level]

            return total

        def get_vwap(row, side):
            total_price = 0
            total_vol = 0

            for level in ['4', '3', '2', '1']:
                total_price += row[side + 'Vol' + level] * row[side + 'Price' + level]
                total_vol += row[side + 'Vol' + level]

            return total_price / total_vol if total_vol != 0 else pd.NA

        # get orderbook
        try:
            self.order_book_data = pd.read_csv(os.path.join(custom_log_path, ORDER_BOOK_FILE_NAME))
            self.order_book_data = self.order_book_data[self.order_book_data['instrument'] == 'ETF']

            # build columns which give the leading, average and trailing prices
            self.order_book_data['leading_bid'] = self.order_book_data.apply(lambda row: row['bidPrice0'] if row['bidPrice0'] != 0 else pd.NA, axis=1)
            self.order_book_data['trailing_bid'] = self.order_book_data.apply(lambda row: get_least_competitive_price(row, "bid"), axis=1)
            self.order_book_data['leading_ask'] = self.order_book_data.apply(lambda row: row['askPrice0'] if row['askPrice0'] != 0 else pd.NA, axis=1)
            self.order_book_data['trailing_ask'] = self.order_book_data.apply(lambda row: get_least_competitive_price(row, "ask"), axis=1)
        except FileNotFoundError:
            pass

        # get the trade ticks
        try:
            self.trade_ticks = pd.read_csv(os.path.join(custom_log_path, TRADE_TICKS_FILE_NAME))
            self.trade_ticks = self.trade_ticks[self.trade_ticks['instrument'] == 'ETF']

            # build columns which give the leading, average and trailing prices
            self.trade_ticks['total_bids'] = self.trade_ticks.apply(lambda row: get_total_orders(row, "bid"), axis=1)
            self.trade_ticks['total_asks'] = self.trade_ticks.apply(lambda row: get_total_orders(row, "ask"), axis=1)
            self.trade_ticks['average_bid'] = self.trade_ticks.apply(lambda row: get_vwap(row, "bid"), axis=1)
            self.trade_ticks['average_ask'] = self.trade_ticks.apply(lambda row: get_vwap(row, "ask"), axis=1)

        except FileNotFoundError:
            pass

    def build_static_plots(self):
        # builds the static plots that changed during runtime

        # break if we don't have the required market data
        if (self.match_events is None) or (self.scoreboard is None):
            return

        # we only sample this many data points as it's too detailed for the dashboard
        scoreboard = self.scoreboard

        # pnl plots
        all_traders_pnl_fig = px.line(scoreboard, y="ProfitOrLoss", x="Time", color="Team", title="All Trader PnL's")

        # exposure plot
        our_trader = scoreboard.loc[scoreboard["Team"] == TRADER_NAME]
        our_trader["TotalPosition"] = our_trader["FuturePosition"] + our_trader["EtfPosition"]
        our_exposure_fig = px.line(our_trader, y="TotalPosition", x="Time", title="Total Exposure")

        ########################
        # work out summary statistics for trading activity
        trade_statistics = pd.DataFrame(columns=["Trader", ComponentNames.TRADES_PER_SECOND, ComponentNames.CANCEL_RATIO, ComponentNames.LOTS_PER_TRADE])

        # loop through each trader
        end_time = scoreboard.iloc[len(scoreboard)-2]["Time"]
        all_traders = list(set(self.scoreboard["Team"]))
        for trader in all_traders:
            # calculate the stats
            this_traders_events = self.match_events.loc[self.match_events["Competitor"] == trader]
            orders_sent = len(this_traders_events.loc[self.match_events["Operation"] == "Insert"])
            orders_cancelled = len(this_traders_events.loc[self.match_events["Operation"] == "Cancel"])
            lots_traded = sum(this_traders_events.loc[self.match_events["Operation"] == "Trade"]["Volume"])
            total_trades = len(this_traders_events.loc[self.match_events["Operation"] == "Trade"])

            # store them
            trade_statistics.loc[len(trade_statistics) - 1] = {
                "Trader": trader,
                ComponentNames.TRADES_PER_SECOND: lots_traded / end_time,
                ComponentNames.CANCEL_RATIO: orders_cancelled / orders_sent,
                ComponentNames.LOTS_PER_TRADE: lots_traded / total_trades
            }

        # build the plots
        trades_per_second_fig = px.bar(trade_statistics, x="Trader", y=ComponentNames.TRADES_PER_SECOND, title=ComponentNames.TRADES_PER_SECOND.value)
        cancel_ratio_fig = px.bar(trade_statistics, x="Trader", y=ComponentNames.CANCEL_RATIO, title=ComponentNames.CANCEL_RATIO.value)
        lots_per_trade_fig = px.bar(trade_statistics, x="Trader", y=ComponentNames.LOTS_PER_TRADE, title=ComponentNames.LOTS_PER_TRADE.value)

        # set all the plots
        try:
            self.tab_data.__setitem__(id=ComponentNames.OUR_EXPOSURE.value, item=dcc.Graph(figure=our_exposure_fig))
            self.tab_data.__setitem__(id=ComponentNames.ALL_TRADERS_PNL.value, item=dcc.Graph(figure=all_traders_pnl_fig))
            self.tab_data.__setitem__(id=ComponentNames.TRADES_PER_SECOND.value, item=dcc.Graph(figure=trades_per_second_fig))
            self.tab_data.__setitem__(id=ComponentNames.CANCEL_RATIO.value, item=dcc.Graph(figure=cancel_ratio_fig))
            self.tab_data.__setitem__(id=ComponentNames.LOTS_PER_TRADE.value, item=dcc.Graph(figure=lots_per_trade_fig))
        except KeyError:
            # sometimes we get key errors due to flag
            pass

    def build_dynamic_plot(self, options={}):
        # builds the dynamic plot which is controlled by checkboxes
        # todo: plot futures mid
        # todo: work out why future trades filled aren't being logged
        # todo: plot orderbook <---
        # todo: plot profit
        # todo: plot other trades happening
        # todo: plot arbitrary signals

        # read the options
        show_etf_mid = ChecklistOptions.ETF_MID.value in options
        show_futures_mid = ChecklistOptions.FUTURES_MID.value in options
        show_etf_set = ChecklistOptions.ETF_TRADES_SENT.value in options
        show_etf_filled = ChecklistOptions.ETF_TRADES_FILLED.value in options
        show_futures_sent = ChecklistOptions.FUTURES_TRADES_SENT.value in options
        show_futures_filled = ChecklistOptions.FUTURES_TRADES_FILLED.value in options
        show_order_book = ChecklistOptions.ORDER_BOOK.value in options
        show_trade_ticks = ChecklistOptions.SHOW_TRADE_TICKS.value in options

        # get a plot with a secondary y-axis
        fig = plotly.subplots.make_subplots(specs=[[{"secondary_y": True}]])

        # set the gridlines to align with ticks and orderbook data
        fig.update_yaxes(tick0=0, dtick=1000, secondary_y=False)
        fig.update_xaxes(tick0=0, dtick=10)
        fig.update_layout(xaxis_rangeslider_visible=False)

        # plot the etf mid
        if (self.prices is not None) and show_etf_mid:
            fig.add_trace(
                graph_objects.Scatter(
                    x=self.prices[self.prices["instrument"] == "ETF"]["time"],
                    y=self.prices[self.prices["instrument"] == "ETF"]["mid"],
                    name="ETF Mid",
                    mode="lines",
                    hoverinfo="none",
                    line=dict(color='rgba(0, 30, 30, 0.3)')
                ),
                secondary_y=False
            )

        # plot the trades sent
        instruments = []
        if show_etf_set: instruments += ["ETF"]
        if show_futures_sent: instruments += ["Future"]
        if self.trades_sent is not None:
            for instrument in instruments:
                for side in ["BUY", "SELL"]:
                    fig.add_trace(
                        graph_objects.Scatter(
                            x=self.trades_sent[self.trades_sent["instrument"] == instrument][self.trades_sent["side"] == side]["time"],
                            y=self.trades_sent[self.trades_sent["instrument"] == instrument][self.trades_sent["side"] == side]["price"],
                            mode='markers',
                            name=instrument + " " + side + " sent",
                            hoverinfo="none",
                            marker=dict(
                                size=4,
                                color=('rgba(255, 0, 0, 0.5)' if side == "BUY" else 'rgba(0, 0, 255, 0.5)') if instrument == "ETF" else
                                      ('rgba(255, 0, 100, 0.5)' if side == "BUY" else 'rgba(0, 100, 255, 0.5)')
                            )
                        ),
                        secondary_y=False
                    )

        # plot the trades filled
        instruments = []
        if show_etf_filled: instruments += ["ETF"]
        if show_futures_filled: instruments += ["Future"]
        if self.trades_filled is not None:
            for instrument in instruments:
                for side in ["BUY", "SELL"]:
                    fig.add_trace(
                        graph_objects.Scatter(
                            x=self.trades_filled[self.trades_filled["instrument"] == instrument][self.trades_filled["side"] == side]["time"],
                            y=self.trades_filled[self.trades_filled["instrument"] == instrument][self.trades_filled["side"] == side]["price"],
                            name=instrument + " " + side + " filled",
                            mode='markers',
                        hoverinfo="none",
                            marker=dict(
                                size=6,
                                color=('rgba(255, 100, 0, 1)' if side == "BUY" else 'rgba(0, 100, 255, 1)') if instrument == "ETF" else
                                      ('rgba(100, 255, 0, 1)' if side == "BUY" else 'rgba(0, 255, 100, 1)'),
                                symbol="diamond",
                                line=dict(color='Black', width=1)
                            )
                        ),
                        secondary_y=False
                    )

        # plot the order book
        if (self.order_book_data is not None) and show_order_book:
            # these candlesticks have open=close, so that only the whiskers are visible
            # this reduces visual clutter
            fig.add_trace(
                graph_objects.Candlestick(
                    x=self.order_book_data["time"],
                    open=self.order_book_data["leading_bid"],
                    close=self.order_book_data['leading_bid'],
                    high=self.order_book_data["leading_bid"],
                    low=self.order_book_data["trailing_bid"],
                    hoverinfo="none",
                    whiskerwidth=0.25,
                    increasing_line_color='rgba(255, 0, 0, 0.2)',
                    decreasing_line_color='rgba(255, 0, 0, 0.2)',
                    name="Orderbook Bids"
                ),
                secondary_y=False
            )
            fig.add_trace(
                graph_objects.Candlestick(
                    x=self.order_book_data["time"],
                    open=self.order_book_data["trailing_ask"],
                    close=self.order_book_data['trailing_ask'],
                    high=self.order_book_data["trailing_ask"],
                    low=self.order_book_data["leading_ask"],
                    hoverinfo="none",
                    increasing_line_color='rgba(0, 0, 255, 0.2)',
                    decreasing_line_color='rgba(0, 0, 255, 0.2)',
                    name="Orderbook Asks"
                ),
                secondary_y=False
            )

        # plot the trade ticks
        if (self.trade_ticks is not None) and show_trade_ticks:
            for side in ["bid", "ask"]:
                fig.add_trace(
                    graph_objects.Scatter(
                        x=self.trade_ticks["time"],
                        y=self.trade_ticks["average_" + side],
                        name=side + "average trade ticks",
                        hovertext=self.trade_ticks["total_" + side + "s"],
                        mode='markers',
                        marker=dict(
                            size=10,
                            color='rgba(180, 150, 100, 0.5)'
                        )
                    ),
                    secondary_y=False
                )

        # add the plot to our tab_data
        try:
            self.tab_data.__setitem__(id=ComponentNames.TRADER_ACTIVITY.value, item=dcc.Graph(figure=fig))
        except KeyError:
            # sometimes we get key errors due to flag
            pass

    def build_container(self):
        # this container stores all the elements of our tab. plots, checklists, titles ect..
        self.tab_data = dbc.Container(
            [
                dbc.Row([
                    dbc.Col(
                        dcc.Graph(id=ComponentNames.TRADER_ACTIVITY.value)
                    )
                ]),
                dbc.Row([
                    dbc.Col(dcc.Graph(id=ComponentNames.CANCEL_RATIO.value)),
                    dbc.Col(dcc.Graph(id=ComponentNames.LOTS_PER_TRADE.value)),
                    dbc.Col(dcc.Graph(id=ComponentNames.TRADES_PER_SECOND.value))
                ]),
                dbc.Row([
                    dbc.Col(dcc.Graph(id=ComponentNames.ALL_TRADERS_PNL.value)),
                    dbc.Col(dcc.Graph(id=ComponentNames.OUR_EXPOSURE.value))
                ])
            ])

    def __init__(self, market_data_folder_path):
        # init some datasets
        self.prices = None
        self.scoreboard = None
        self.match_events = None
        self.trades_sent = None
        self.trades_filled = None
        self.trades_pnl = None
        self.trade_ticks = None
        self.order_book_data = None

        # path to market data
        self.market_data_folder_path = market_data_folder_path

        # build the tab
        self.tab_data = None  # this is what is sent to dash to display the tab

        self.build_container()
        self.import_data()
        self.build_static_plots()
        self.build_dynamic_plot()


@callback(Output('main', 'children', allow_duplicate=True),
          Input(ComponentNames.DYNAMIC_GRAPH_CHECKLIST.value, 'value'),
          config_prevent_initial_callbacks=True)
def change_plot_options(options):
    if CURRENT_TAB == DEFAULT_TAB:
        return

    # rebuild the plots
    TABS[CURRENT_TAB].build_container()
    TABS[CURRENT_TAB].build_static_plots()
    TABS[CURRENT_TAB].build_dynamic_plot(options)

    # return the tab data
    return TABS[CURRENT_TAB].tab_data


# called when we select a tab in the web app
@callback(Output('main', 'children'),
          Input(ComponentNames.TAB_BUTTONS.value, 'value'))
def switch_tab(tab):
    global CURRENT_TAB
    # if the default tab is selected, refresh the tab list
    if tab == DEFAULT_TAB:
        CURRENT_TAB = tab
        update_tab_list()
        return TABS[DEFAULT_TAB]

    # else load the tab
    if tab not in TABS:
        print("[Error] Tab not found")
        return
    else:
        CURRENT_TAB = tab
        return TABS[tab].tab_data


# imports market data and builds a tab from it
def build_tab(market_data_folder):
    market_data_folder_path = os.path.join(PATH, market_data_folder)
    return Tab(market_data_folder_path)


# updates the list of tabs
def update_tab_list():
    # In DEV_MODE, we only consider market data files from the current working directory.
    # When not in DEV_MODE, we build multiple tabs for multiple folders of market data,
    #   every folder must thus contain market data
    if DEV_MODE == "true":
        new_tab = build_tab(market_data_folder=".")
        TABS["."] = new_tab
    else:
        # get market data folders
        market_data_folders = [name for name in os.listdir(PATH) if os.path.isdir(os.path.join(PATH, name))]

        # build the tabs we haven't yet built
        for folder in market_data_folders:
            if folder not in TABS:
                new_tab = build_tab(market_data_folder=folder)

                # if built successfully, save it down
                if new_tab:
                    TABS[folder] = new_tab

    # assemble the tabs to pass through to the dashboard
    assembled_tabs = [
        dcc.Tab(label=market_data, value=market_data) for market_data in TABS.keys()
    ]

    # assemble the dashboard
    app.layout = html.Div([
        html.H1('NAlex RTG Dashboard'),
        dcc.Tabs(id=ComponentNames.TAB_BUTTONS.value, value=DEFAULT_TAB, children=
            assembled_tabs
        ),
        dbc.Row([
            dbc.Col(
                dcc.Checklist(
                options=[option.value for option in ChecklistOptions],
                value=[],
                inline=True,
                id=ComponentNames.DYNAMIC_GRAPH_CHECKLIST.value,
                inputStyle={'display': 'inline-block', "margin-right": "5px", "margin-left": "20px"})
            )
        ]),
        html.Div(id='main', children=[])  # div to which we insert the tabs content
    ])


# function to start the dashboard
def launch_dashboard(path, trader_name, dev_mode):
    global PATH, TRADER_NAME, DEV_MODE, app
    PATH = path
    TRADER_NAME = trader_name
    DEV_MODE = dev_mode

    app = Dash(external_stylesheets=[dbc.themes.BOOTSTRAP])
    update_tab_list()
    app.run(debug=True)


# alternative command line interface
if __name__ == "__main__":
    # parse arguments
    parser = argparse.ArgumentParser(description="arguments")
    parser.add_argument("--dev_mode", required=True, help="In dev mode, the dashboard directly log files, instead of searching for them", dest="dev_mode")
    parser.add_argument("--path", required=True, help="Path containing our round data", dest="path")
    parser.add_argument("--trader_name", required=True, help="Name of the trader to analyse", dest="trader_name")
    args = parser.parse_args()

    launch_dashboard(args.path, args.trader_name, args.dev_mode)
