var _ = require("lodash");
const express = require("express");
const fs = require("fs");
const { Telegraf } = require("telegraf");

const bot = new Telegraf(process.env.BOT_TOKEN);
var chat_id;
bot.start((ctx) => {
  chat_id = ctx.chat.id;
  ctx.reply("Welcome");
});
bot.help((ctx) => ctx.reply("Send me a sticker"));
bot.on("sticker", (ctx) => ctx.reply("👍"));
bot.hears("hi", (ctx) => ctx.reply("Hey there"));
bot.launch();

const app = express();
// TODO: Integrate into https
app.use(express.urlencoded({ extended: true }));
app.use(express.json());
const port = 80;

app.post("/api/:id", async (req, res) => {
  let id = req.params.id;
  let data = req.params.data;
  console.log(req.body.data);

  console.log(`Post request received from: ${id} and containing : ${data}`);
  bot.sendMessage();

  res.send({ status: "okay" });
});

app.listen(port, () => {
  console.log(`App listening at http://localhost:${port}`);
});

// Enable graceful stop
process.once("SIGINT", () => bot.stop("SIGINT"));
process.once("SIGTERM", () => bot.stop("SIGTERM"));
