const express = require("express");
const client = require("twilio")(process.env.ACCOUNT_SID, process.env.API_KEY);

function sendAlerts(req, res) {
  let id = req.params.id;
  console.log(`Alert for ${id} issued`);
  client.messages
    .create({
      messagingServiceSid: process.env.MESSAGING_SERVICE_SID,
      to: process.env.NUMBER_TO_SEND,
      body: "fall detected!",
    })
    .then((message) => console.log(message.sid))
    .done();

  res.status(200).send("okay");
}

function getDefault(req, res) {
  res.status(404).send("Bad URL");
}

// Create an Express object and routes (in order)
const app = express();

app.use("/alerts/:id", sendAlerts);
app.use(getDefault);

// Set our GCF handler to our Express app.
exports.api = app;
