import { PreComputeManager } from "./precompute";
import { RequestHandler } from "./request_handler";
import * as express from "express";
require("dotenv").config();

const IS_DEPLOY = process.env.DEPLOY == "true";
console.log("IS_DEPLOY = ", IS_DEPLOY, !IS_DEPLOY);
const TETRIS_TRAINER_URL = "https://gregorycannon.github.io";

const port = process.env.PORT || 3002;
const ALLOW_MULTITHREAD = !IS_DEPLOY;

function initExpressServer(requestHandler) {
  const app = express();

  // Set CORS policy
  app.use((req, res, next) => {
    res.setHeader(
      "Access-Control-Allow-Origin",
      "*"
      // IS_DEPLOY ? TETRIS_TRAINER_URL : "*"
    );
    next();
  });

  app.get("*", async function (req: any, res: any) {
    if (req.url == "/favicon.ico") {
      return res.end("");
    }
    // Log the request
    console.log("\n-------------------------\nlocalhost:" + port + req.url);
    console.time("Full request");

    // Main processing
    let response, responseCode;
    try {
      [response, responseCode] = await requestHandler.routeRequest(req);
    } catch (err) {
      console.error(err);
      responseCode = 500; // Internal Server Error
      response = err.message;
    }

    // Send response
    console.timeEnd("Full request");
    // console.log("Sending response:", response, responseCode);
    res.setHeader("Content-Type", "text/plain");
    res.statusCode = responseCode;
    res.end(response);
  });

  app.listen(port);
  console.log("Listening on port", port);
}

if (ALLOW_MULTITHREAD) {
  // Create an object to manage the worker threads involved in heavy placement computation
  const precomputer = new PreComputeManager();

  // Start the server once the precomputer is operational
  precomputer.initialize(() => {
    const requestHandler = new RequestHandler(precomputer);
    initExpressServer(requestHandler);
  });
} else {
  const requestHandler = new RequestHandler(null);
  initExpressServer(requestHandler);
}
