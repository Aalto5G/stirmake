#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <iostream>

class Cmd {
  public:
    std::vector<std::string> args;
    Cmd() {}
    Cmd(std::vector<std::string> v): args(v) {}
};

class Rule {
  public:
    bool phony;
    bool executed;
    bool executing;
    bool queued;
    std::vector<std::string> tgts;
    std::vector<std::string> deps;
    Cmd cmd;
    int ruleid;

    Rule(): phony(false), executed(false), executing(false), queued(false) {}
};

int children = 0;
const int limit = 2;

std::vector<Rule> rules;

std::map<std::string, int> ruleid_by_tgt;
std::map<std::string, std::set<int>> ruleids_by_dep;

void add_rule(std::vector<std::string> tgts,
              std::vector<std::string> deps,
              std::vector<std::string> cmdargs,
              bool phony)
{
  Rule r;
  Cmd c(cmdargs);
  r.phony = phony;
  if (tgts.size() <= 0)
  {
    abort();
  }
  if (phony && tgts.size() != 1)
  {
    abort();
  }
  r.tgts = tgts;
  r.deps = deps;
  r.cmd = c;
  r.ruleid = rules.size();
  rules.push_back(r);
  for (auto it = tgts.begin(); it != tgts.end(); it++)
  {
    if (ruleid_by_tgt.find(*it) != ruleid_by_tgt.end())
    {
      std::cerr << "duplicate rule" << std::endl;
      exit(1);
    }
    ruleid_by_tgt[*it] = r.ruleid;
  }
  for (auto it = deps.begin(); it != deps.end(); it++)
  {
    if (ruleids_by_dep.find(*it) == ruleids_by_dep.end())
    {
      ruleids_by_dep[*it] = std::set<int>();
    }
    ruleids_by_dep[*it].insert(r.ruleid);
  }
}

std::vector<int> ruleids_to_run;

std::map<pid_t, int> ruleid_by_pid;

pid_t fork_child(int ruleid)
{
  std::vector<char*> args;
  pid_t pid;
  Cmd cmd = rules.at(ruleid).cmd;

  pid = fork();
  if (pid < 0)
  {
    abort();
  }
  else if (pid == 0)
  {
    for (auto it = cmd.args.begin(); it != cmd.args.end(); it++)
    {
      args.push_back(strdup(it->c_str()));
    }
    args.push_back(NULL);
    execvp(args[0], &args[0]);
    //write(1, "Err\n", 4);
    _exit(1);
  }
  else
  {
    children++;
    ruleid_by_pid[pid] = ruleid;
    return pid;
  }
}

void consider(int ruleid)
{
  Rule &r = rules.at(ruleid);
  int toexecute = 0;
  std::cout << "considering " << r.tgts[0] << std::endl;
  if (r.executed)
  {
    std::cout << "already execed " << r.tgts[0] << std::endl;
    return;
  }
  if (r.executing)
  {
    std::cout << "already execing " << r.tgts[0] << std::endl;
    return;
  }
  r.executing = true;
  for (auto it = r.deps.begin(); it != r.deps.end(); it++)
  {
    if (ruleid_by_tgt.find(*it) != ruleid_by_tgt.end())
    {
      consider(ruleid_by_tgt[*it]);
      if (!rules.at(ruleid_by_tgt[*it]).executed)
      {
        toexecute = 1;
      }
    }
  }
/*
  if (r.phony)
  {
    r.executed = true;
    break;
  }
*/
  if (!toexecute && !r.queued)
  {
    ruleids_to_run.push_back(ruleid);
    r.queued = true;
  }
/*
  ruleids_to_run.push_back(ruleid);
  r.executed = true;
*/
}

void reconsider(int ruleid)
{
  Rule &r = rules.at(ruleid);
  int toexecute = 0;
  std::cout << "reconsidering " << r.tgts[0] << std::endl;
  if (r.executed)
  {
    std::cout << "already execed " << r.tgts[0] << std::endl;
    return;
  }
  if (!r.executing)
  {
    abort();
  }
  for (auto it = r.deps.begin(); it != r.deps.end(); it++)
  {
    int dep = ruleid_by_tgt[*it];
    if (!rules.at(dep).executed)
    {
      toexecute = 1;
      break;
    }
  }
  if (!toexecute && !r.queued)
  {
    ruleids_to_run.push_back(ruleid);
    r.queued = true;
  }
}

void mark_executed(int ruleid)
{
  Rule &r = rules.at(ruleid);
  if (r.executed)
  {
    abort();
  }
  if (!r.executing)
  {
    abort();
  }
  r.executed = true;
  for (auto it = r.tgts.begin(); it != r.tgts.end(); it++)
  {
    std::set<int> &s = ruleids_by_dep[*it];
    for (auto it2 = s.begin(); it2 != s.end(); it2++)
    {
      reconsider(*it2);
    }
  }
}

int main(int argc, char **argv)
{
  std::vector<std::string> v_all{"all"};
  std::vector<std::string> v_ab{"a.txt", "b.txt"};
  std::vector<std::string> v_c{"c.txt"};
  std::vector<std::string> arge{"echo", "2"};
  std::vector<std::string> argt{"touch", "a.txt", "b.txt"};

  add_rule(v_all, v_ab, arge, 1);
  add_rule(v_ab, v_c, argt, 0);

  consider(0);

  while (children < limit && !ruleids_to_run.empty())
  {
    std::cout << "forking1 child" << std::endl;
    fork_child(ruleids_to_run.back());
    ruleids_to_run.pop_back();
  }

  for (;;)
  {
    int wstatus = 0;
    pid_t pid;
    pid = wait(&wstatus);
    if (wstatus != 0 && pid > 0)
    {
      if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
      {
        std::cout << "error exit status" << std::endl;
        std::cout << WIFEXITED(wstatus) << std::endl;
        std::cout << WIFSIGNALED(wstatus) << std::endl;
        std::cout << WEXITSTATUS(wstatus) << std::endl;
        return 1;
      }
    }
    if (children <= 0)
    {
      if (pid < 0 && errno == ECHILD)
      {
        return 0;
      }
      abort();
    }
    if (pid < 0)
    {
      abort();
    }
    int ruleid = ruleid_by_pid[pid];
    if (ruleid_by_pid.erase(pid) != 1)
    {
      abort();
    }
    mark_executed(ruleid);
    children--;
    while (children < limit && !ruleids_to_run.empty())
    {
      std::cout << "forking child" << std::endl;
      fork_child(ruleids_to_run.back());
      ruleids_to_run.pop_back();
    }
  }
  return 0;
}
