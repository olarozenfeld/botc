package com.botc.game;

import com.botc.game.GameLog.BotcLog;
import com.google.protobuf.TextFormat;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.logging.Logger;

/** Simple CP Program.*/
public class GameState {
  private GameState() {}

  private static BotcLog getGameLogFromFile(File file) throws IOException {
    BotcLog.Builder builder = BotcLog.newBuilder();
    try (FileInputStream fin = new FileInputStream(file)) {
      TextFormat.getParser().merge(new InputStreamReader(fin), builder);
    }
    return builder.build();
  }
  
  private static final Logger logger = Logger.getLogger(GameState.class.getName());

  public static void main(String[] args) throws Exception {
    logger.info("Hello, world?");
    BotcLog g = getGameLogFromFile(new File(args[0]));
    logger.info(TextFormat.printToString(g));
  }
}
