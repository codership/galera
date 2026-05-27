//

pipeline {
  agent { label 'built-in' }
  stages {

    stage ('Smoke Test') {
      steps {
        script {
          def desc = ghprbPullLongDescription.replace('\\r', '').replace('\\n', '\n')
          def branchLine = desc.readLines().find { it.startsWith('MYSQL_BRANCH=') }
          if (branchLine) {
            def mysqlBranch = branchLine.substring('MYSQL_BRANCH='.length()).trim()
            if (!(mysqlBranch ==~ /[A-Za-z0-9._\/-]+/)) {
              error "Invalid MYSQL_BRANCH value in PR description: ${mysqlBranch}"
            }
            env.MYSQL_BRANCH = mysqlBranch
          } else {
            env.MYSQL_BRANCH = env.DEFAULT_MYSQL_BRANCH
          }

          build job: 'pr-galera-4.x-smoke-test', wait: true,
                  parameters: [ string(name: 'GALERA_BRANCH', value: env.ghprbActualCommit )]
        }
      }
    }

    stage ('Build') {
      steps {
        script {
          def bintarJob = build job: 'pr-build-galera-4.x-mysql-8.0-v26', wait: true,
            parameters: [
              string(name: 'GALERA_BRANCH', value: env.ghprbActualCommit ),
              string(name: 'MYSQL_BRANCH', value: env.MYSQL_BRANCH)
              ]
          env.BINTAR_JOB = bintarJob.getNumber().toString()
        }
      }
    }

    stage ('Run Tests') {
      parallel {
        stage ('MTR') {
          steps {
            build job: 'pr-mtr-galera-4.x-mysql-8.0-v26', wait: true,
              parameters: [ string(name: 'BUILD_SELECTOR', value: env.BINTAR_JOB) ]
          }
        }
        stage ('GcTest') {
          steps {
            build job: 'pr-sssc-galera-4.x-mysql-8.0-v26', wait: true,
              parameters: [ string(name: 'BUILD_SELECTOR', value: env.BINTAR_JOB) ]
          }
        }
      } // parallel
    }
  }
}
