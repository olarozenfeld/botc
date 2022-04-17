package com.botc.game;

import com.botc.game.GameLogProto.GameLog;
import com.botc.game.GameLogProto.Perspective;
import com.botc.game.GameLogProto.Setup;
import com.google.protobuf.TextFormat;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.logging.Logger;

// Holds the full state of a game up to a given point, from
// a particular perspective.
public class GameState {
  private String[] playerNames;
  private int numOutsiders, numMinions;
  private Perspective perspective;

  // Starting role counts by cunmber of players 5-15. Always 1 demon.
  private static int[] NUM_TOWNSFOLK = {3, 3, 5, 5, 5, 7, 7, 7, 9, 9, 9};
  private static int[] NUM_OUTSIDERS = {0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 2};
  private static int[] NUM_MINIONS = {1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3};

  private GameState() {}

  // Loads the serialized game state from a text game_log proto.
  public static GameState fromTextFile(File file) throws IOException {
    GameLog.Builder builder = GameLog.newBuilder();
    try (FileInputStream fin = new FileInputStream(file)) {
      TextFormat.getParser().merge(new InputStreamReader(fin), builder);
    }
    return fromProto(builder.build());
  }

  // Checks that outsider and minion counts are valid according to rules.
  private static void checkRoleCountValidity(int numPlayers, int numOutsiders, int numMinions) {
    // Without travelers, we support up to 15 players.
    if (numPlayers < 5 || numPlayers > 15) {
      throw new IllegalArgumentException("Expected 5-15 players, got " + numPlayers);
    }
    int expectedTownsfolk = NUM_TOWNSFOLK[numPlayers-5];
    int expectedOutsiders = NUM_OUTSIDERS[numPlayers-5];
    int expectedMinions = NUM_MINIONS[numPlayers-5];
    int numTownsfolk = numPlayers - numOutsiders - numMinions - 1;
    if (expectedOutsiders != numOutsiders || expectedMinions != numMinions) {
      throw new IllegalArgumentException(String.format(
        "Expected values for %d players <townsfolk, outsiders, minions, demons>: " +
        "<%d,%d,%d,1>, got: <%d,%d,%d,1>", numPlayers, expectedTownsfolk, expectedOutsiders,
        expectedMinions, numTownsfolk, numOutsiders, numMinions));
    }
  }

  // Loads the game state from a game_log proto.
  public static GameState fromProto(GameLog log) {
    GameState g = new GameState();
    if (log.getPerspective() == Perspective.PERSPECTIVE_UNSPECIFIED) {
      throw new IllegalArgumentException("Expected a valid perspective");
    }
    g.perspective = log.getPerspective();
    Setup s = log.getSetup();
    if (s == null) {
      throw new IllegalArgumentException("Setup section is missing");
    }
    checkRoleCountValidity(s.getPlayersCount(), s.getNumOutsiders(), s.getNumMinions());
    g.playerNames = new String[s.getPlayersCount()];
    for (int i = 0; i < s.getPlayersCount(); i++) {
      g.playerNames[i] = s.getPlayers(i);
    }
    g.numOutsiders = s.getNumOutsiders();
    g.numMinions = s.getNumMinions();

    if (g.perspective == Perspective.STORYTELLER) {

    }
    return g;
  }

  public GameLog toProto() {
    GameLog.Builder builder = GameLog.newBuilder();
    return builder.build();    
  }

  private static final Logger logger = Logger.getLogger(GameState.class.getName());

  public static void main(String[] args) throws Exception {
    logger.info("Hello, world?");
    GameState g = fromTextFile(new File(args[0]));
    logger.info(TextFormat.printToString(g.toProto()));
  }
}
